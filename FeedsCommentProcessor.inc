<?php
/**
 * @file
 * Contains FeedsCommentProcessor.
 *
 * Backdrop comment node-status constants (defined in comment.module):
 * COMMENT_NODE_HIDDEN = 0 - comments and comment form are hidden entirely
 * COMMENT_NODE_CLOSED = 1 - no new comments allowed; existing ones visible
 * COMMENT_NODE_OPEN   = 2 - comments open; new comments can be posted
 *
 * Backdrop comment bundle naming convention: 'comment_node_' . $node_type
 * e.g. for the default 'post' content type the bundle is 'comment_node_post'.
 */

/**
 * Creates comments from feed items.
 */
class FeedsCommentProcessor extends FeedsProcessor {

  /**
   * {@inheritdoc}
   *
   * Comments in Backdrop do not have independent bundles; bundle behavior is
   * inherited from the parent node type. Return FALSE so the parent class does
   * not require a bundle to be explicitly selected in the importer UI.
   */
  public function bundleRequired() {
    return FALSE;
  }

  /**
   * {@inheritdoc}
   *
   * The bundle key for comment entities in Backdrop is 'node_type', which
   * stores the full prefixed bundle name (e.g. 'comment_node_post').
   */
  public function bundleKey() {
    return 'node_type';
  }

  /**
   * {@inheritdoc}
   *
   * Return the configured bundle prefixed with 'comment_node_', or fall back
   * to a sensible default. The actual node_type on each comment object is set
   * dynamically in entityValidate() once the target node is known.
   */
  public function bundle() {
    $node_type = $this->config['node_type'] ?? '';
    if ($node_type) {
      // Strip any existing prefix so we don't double-prefix.
      $node_type = preg_replace('/^comment_node_/', '', $node_type);
      return 'comment_node_' . $node_type;
    }
    // Fall back to the first available node type.
    $types = node_type_get_types();
    if ($types) {
      $first = key($types);
      return 'comment_node_' . $first;
    }
    return 'comment_node_post';
  }

  /**
   * Define entity type.
   */
  public function entityType() {
    return 'comment';
  }

  /**
   * {@inheritdoc}
   */
  protected function entityInfo() {
    $info = parent::entityInfo();
    $info['label plural'] = t('Comments');
    return $info;
  }

  /**
   * {@inheritdoc}
   *
   * Use entity_create() so that any site-level hook_entity_info_alter() that
   * substitutes a custom Comment subclass is respected.
   *
   * entity_create() accepts an array of initial property values and returns a
   * fully-formed Comment object (which extends Entity, which extends stdClass).
   */
  protected function newEntity(FeedsSource $source) {
    $comment = entity_create('comment', array(
      'cid'       => 0,
      'pid'       => 0,
      'nid'       => 0,
      'uid'       => $this->config['author'],
      // node_type (the bundle) is set correctly in entityValidate() once the
      // nid mapping target has been resolved and the node loaded. The value
      // must be the prefixed bundle name 'comment_node_<type>'.
      'node_type' => '',
      'hostname'  => '',
      'created'   => REQUEST_TIME,
      'changed'   => REQUEST_TIME,
      'langcode'  => LANGUAGE_NONE,
      'name'      => '',
      'mail'      => '',
      'subject'   => '',
    ));
    // Flag used by entitySave() to distinguish a brand-new comment (cid = 0)
    // from an existing comment being updated via a mapped cid target.
    $comment->feeds_is_new = TRUE;

    return $comment;
  }

  /**
   * {@inheritdoc}
   */
  protected function entityValidate($entity, ?FeedsSource $source = NULL) {
    $comment = $entity;

    // Validate Node ID exists.
    if (empty($comment->nid)) {
      throw new FeedsValidationException(t('Cannot create comment: No Node ID (nid) provided. Please map nid, nid_by_guid, or nid_by_title.'));
    }

    // Load the target node.
    $node = node_load($comment->nid);
    if (!$node) {
      throw new FeedsValidationException(t('Cannot create comment: Node @nid does not exist.', array('@nid' => $comment->nid)));
    }

    // Set bundle name before comment_save() so field data is saved correctly.
    $comment->node_type = 'comment_node_' . $node->type;

    // Check if comments are allowed on this content type in node type settings.
    $node_type = node_type_load($node->type);
    if ($node_type) {
      $comment_setting = $node_type->settings['comment_default'] ?? COMMENT_NODE_OPEN;
      if ($comment_setting == COMMENT_NODE_HIDDEN) {
        throw new FeedsValidationException(t('Cannot create comment: Comments are hidden for content type "@type".', array('@type' => $node_type->label)));
      }
    }

    // Validate parent comment if provided.
    if (!empty($comment->pid) && !comment_load($comment->pid)) {
      throw new FeedsValidationException(t('Invalid parent comment id @pid.', array('@pid' => $comment->pid)));
    }

    // Resolve the comment author. $comment->name is set by setTargetElement()
    // from the mapped 'author' column. Try to match it to a registered user
    // account - if found, set uid so the comment is attributed correctly.
    // This must happen here in entityValidate() rather than setTargetElement()
    // because Feeds calls entityValidate() before applying target mappings,
    // and setting uid here ensures it is correctly present for entitySave().
    if (empty($comment->uid) && !empty($comment->name)) {
      if ($account = user_load_by_name($comment->name)) {
        $comment->uid = $account->uid;
      }
    }

    // If name lookup didn't resolve a user, try email lookup as a fallback.
    if (empty($comment->uid) && !empty($comment->mail)) {
      if ($account = user_load_by_mail($comment->mail)) {
        $comment->uid = $account->uid;
      }
    }

    $comment->status = empty($comment->status) ? COMMENT_NOT_PUBLISHED : COMMENT_PUBLISHED;

    // Ensure langcode is set; fall back to LANGUAGE_NONE if not mapped.
    if (empty($comment->langcode)) {
      $comment->langcode = LANGUAGE_NONE;
    }

    $this->prepareCommentSubject($comment);

    if (!empty($comment->mail) && !valid_email_address($comment->mail)) {
      $comment->mail = '';
    }

    // If there is a valid user, populate any missing defaults.
    if ($comment->uid && $account = user_load($comment->uid)) {
      $comment->name = $account->name;
      // Do not store mail for registered users - the mail field on a comment
      // is for anonymous commenters only. Registered user mail comes from their
      // account and is not stored in the {comment} table.
      $comment->mail = '';
      // Unpublish comments by users that need approval.
      if (!user_access('skip comment approval', $account)) {
        $comment->status = COMMENT_NOT_PUBLISHED;
      }
    }
    elseif (!$comment->uid && !$comment->name) {
      $comment->name = config_get('system.core', 'anonymous');
    }
  }

  /**
   * {@inheritdoc}
   */
  protected function entitySaveAccess($entity, ?FeedsSource $source = NULL) {
    $comment = $entity;

    if (!$this->config['authorize'] || !$comment->uid) {
      return;
    }

    $author = user_load($comment->uid);
    if (!$author) {
      throw new FeedsAccessException(t('User %uid is not a valid user.', array('%uid' => $comment->uid)));
    }

    if (!user_access('post comments', $author)) {
      throw new FeedsAccessException(t('User %user is not permitted to post comments.', array('%user' => $author->name)));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function entitySave($entity, ?FeedsSource $source = NULL) {
    $comment = $entity;

    // For existing comments being updated (cid is known, not a new entity),
    // calculate thread if it is missing before passing to comment_save().
    if (empty($comment->feeds_is_new) && !empty($comment->cid)) {
      if (empty($comment->thread)) {
        $comment->thread = $this->calculateThread($comment);
      }
    }

    // When comment_notify is enabled, suppress notification emails with import.
    // Bulk-importing historical comments should not trigger new email
    // notifications to node authors or existing subscribers.
    // Setting $comment->notified = 1 causes _comment_notify_mailalert() to
    // return immediately, skipping both the email send and the token_replace()
    // call on the mail template. That call produces an "Array to string
    // conversion" notice because the [comment:body] token resolves
    // $comment->comment_body directly as a field array rather than a scalar.
    // The notice fires once per distinct node being commented on (from the
    // node author notification block), not once per comment - so importing
    // multiple comments on the same node produces only one notice for that
    // node rather than one per comment.
    // The comment notify record is still written correctly by
    // hook_comment_insert(), and saveCommentNotifySettings() then overwrites
    // it with the mapped notify value immediately after comment_save().
    if (module_exists('comment_notify')) {
      // comment_notify's hook_comment_insert() reads two separate properties:
      // $comment->notify      - boolean, whether the commenter subscribed
      // $comment->notify_type - the subscription type
      // (1 = all comments, 2 = replies to this comment only)
      // Our mapped value uses the same 0/1/2 convention as the {comment_notify}
      // table's 'notify' column, so we split it into the two properties here.
      $notify_value = (int) ($comment->comment_notify_value ?? 0);

      $comment->notify      = $notify_value > 0 ? 1 : 0;
      $comment->notify_type = $notify_value > 0 ? $notify_value : 1;

      // Set notified based on whether the commenter subscribed. If they
      // subscribed (notify > 0), this is a historical import so notifications
      // were already sent in the source system - notified = 1 prevents the
      // destination site sending duplicate emails and suppresses the
      // token_replace() notice from comment_notify's mail handler.
      // If notify = 0 the commenter never subscribed so notified = 0 is
      // correct - there is nothing to have been notified about.
      $comment->notified = $notify_value > 0 ? 1 : 0;
    }

    // Suppress comment_notify emails during import. _comment_notify_mailalert()
    // runs during hook_comment_insert() for any comment with a mapped
    // subscription type, regardless of $comment->notified set above. The
    // feeds_comment_processor_mail_alter() implementation in
    // feeds_comment_processor.module checks this flag and cancels delivery of
    // any comment_notify mail via hook_mail_alter().
    feeds_comment_processor_importing(TRUE);
    comment_save($comment);
    feeds_comment_processor_importing(FALSE);

    // CommentStorageController uses db_update() to maintain
    // {node_comment_statistics},
    // which silently fails if the row doesn't exist (see core issue
    // https://github.com/backdrop/backdrop-issues/issues/5870). This causes
    // comment bodies to be excluded from search indexing since
    // comment_node_update_index() relies on this table. We use db_merge() to
    // ensure the row always exists and is correct after import.
    $last_comment = db_query('SELECT cid, changed, uid, name FROM {comment} WHERE nid = :nid AND status = :status ORDER BY created DESC LIMIT 1', array(
      ':nid'    => $comment->nid,
      ':status' => COMMENT_PUBLISHED,
    ))->fetchObject();
    $count = db_query('SELECT COUNT(cid) FROM {comment} WHERE nid = :nid AND status = :status', array(
      ':nid'    => $comment->nid,
      ':status' => COMMENT_PUBLISHED,
    ))->fetchField();
    if ($last_comment) {
      db_merge('node_comment_statistics')
        ->key(array('nid' => $comment->nid))
        ->fields(array(
          'nid'                    => $comment->nid,
          'cid'                    => $last_comment->cid,
          'last_comment_timestamp' => $last_comment->changed,
          'last_comment_name'      => $last_comment->uid ? '' : $last_comment->name,
          'last_comment_uid'       => $last_comment->uid,
          'comment_count'          => $count,
        ))
        ->execute();
    }

    // Flag the parent node for re-indexing so the imported comment body is
    // included in the next search index update. This is independent of the
    // {node_comment_statistics} update above - search_touch_node() should run
    // for every saved comment regardless of whether the statistics merge found
    // a row to write.
    if (module_exists('search')) {
      search_touch_node($comment->nid);
    }

    // CommentStorageController::preSave() in Backdrop core unconditionally
    // overwrites $comment->hostname with ip_address() (the current request IP),
    // ignoring any hostname value set on the object before comment_save().
    // See core issue https://github.com/backdrop/backdrop-issues/issues/7151.
    // As a workaround, write the mapped hostname directly to the database after
    // comment_save() if one was explicitly provided in the import source.
    if (!empty($comment->hostname_original)) {
      db_update('comment')
        ->fields(array('hostname' => $comment->hostname_original))
        ->condition('cid', $comment->cid)
        ->execute();
    }

    // After save, overwrite the {comment_notify} row that hook_comment_insert()
    // created. We do this because comment_save() may reset entity properties
    // before firing hooks, causing hook_comment_insert() to receive wrong or
    // missing notify/notify_type values. saveCommentNotifySettings() writes the
    // correct values directly from our staging property
    // $comment->comment_notify_value.
    if (module_exists('comment_notify')) {
      $this->saveCommentNotifySettings($comment);
    }
  }

  /**
   * Save Comment Notify settings after a comment has been saved.
   *
   * This method is only called when the comment_notify module is enabled AND
   * a 'comment_notify' mapping target value was provided during import.
   *
   * @param object $comment
   *   The comment object that was just saved (cid must be populated).
   */
  protected function saveCommentNotifySettings($comment) {
    if (!db_table_exists('comment_notify')) {
      return;
    }

    // hook_comment_insert() in comment_notify has already inserted a row into
    // {comment_notify} at this point, but it reads $comment->notify_type from
    // the entity object which may have been reset during comment_save(). We
    // overwrite the row here with correct values from our staging properties.
    $notify_value = (int) ($comment->comment_notify_value ?? 0);

    $notify_data = array(
      'cid'      => $comment->cid,
      'notify'   => $notify_value,
      'notified' => $notify_value > 0 ? 1 : 0,
    );

    // For anonymous commenters with an email address, generate the
    // unsubscribe hash using the same method as comment_notify itself.
    if (empty($comment->uid) && !empty($comment->mail)) {
      if (!empty($comment->comment_notify_hash)) {
        $notify_data['notify_hash'] = $comment->comment_notify_hash;
      }
      else {
        $notify_data['notify_hash'] = backdrop_hmac_base64(
          $comment->mail . $comment->cid,
          backdrop_get_private_key() . backdrop_get_hash_salt()
        );
      }
    }

    db_merge('comment_notify')
      ->key(array('cid' => $comment->cid))
      ->fields($notify_data)
      ->execute();
  }

  /**
   * {@inheritdoc}
   */
  protected function entityDeleteMultiple($entity_ids, ?FeedsSource $source = NULL) {
    comment_delete_multiple($entity_ids);
  }

  /**
   * {@inheritdoc}
   */
  protected function expiryQuery(FeedsSource $source, $time) {
    $select = parent::expiryQuery($source, $time);
    $select->condition('e.created', REQUEST_TIME - $time, '<');

    return $select;
  }

  /**
   * {@inheritdoc}
   */
  public function expiryTime() {
    return $this->config['expire'];
  }

  /**
   * {@inheritdoc}
   */
  public function configDefaults() {
    $defaults = parent::configDefaults();
    $defaults['expire'] = FEEDS_EXPIRE_NEVER;
    $defaults['author'] = 0;
    $defaults['authorize'] = TRUE;
    $defaults['input_format'] = 'filtered_html';
    return $defaults;
  }

  /**
   * {@inheritdoc}
   */
  public function configForm(&$form_state) {
    $form = parent::configForm($form_state);

    // Remove the default bundle selector added by the parent class; comments
    // do not have independently selectable bundles.
    unset($form['bundle']);

    $form['input_format'] = array(
      '#type' => 'select',
      '#title' => t('Comment input format'),
      '#description' => t('Text format applied to imported comment bodies.'),
      '#options' => $this->getAvailableTextFormats(),
      '#default_value' => $this->config['input_format'] ?? 'filtered_html',
    );

    $author = user_load($this->config['author']);
    $form['author'] = array(
      '#type' => 'textfield',
      '#title' => t('Default author'),
      '#description' => t('Username of the default comment author when no author is mapped or the mapped value does not match an existing account.'),
      '#autocomplete_path' => 'user/autocomplete',
      '#default_value' => empty($author->name) ? '' : check_plain($author->name),
    );

    $form['authorize'] = array(
      '#type' => 'checkbox',
      '#title' => t('Check author permissions'),
      '#description' => t('Verify the mapped author has permission to post comments before importing.'),
      '#default_value' => $this->config['authorize'],
    );

    // Expiration settings.
    $period = backdrop_map_assoc(array(
      FEEDS_EXPIRE_NEVER,
      3600,
      10800,
      21600,
      43200,
      86400,
      259200,
      604800,
      604800 * 4,
      604800 * 12,
      604800 * 24,
      31536000,
    ), 'feeds_format_expire');

    $form['expire'] = array(
      '#type' => 'select',
      '#title' => t('Expire comments'),
      '#options' => $period,
      '#description' => t("Delete imported comments after this period. Uses the comment's created date."),
      '#default_value' => $this->config['expire'],
    );

    return $form;
  }

  /**
   * Returns an array of available text format machine names keyed to labels.
   *
   * Caches the result statically to avoid repeated database queries within a
   * single request.
   *
   * @return array
   *   Associative array of format machine_name => human label.
   */
  protected function getAvailableTextFormats() {
    $options = &backdrop_static(__METHOD__, array());

    if (!empty($options)) {
      return $options;
    }

    if (module_exists('filter')) {
      foreach (filter_formats() as $format) {
        $options[$format->format] = $format->name;
      }
    }

    if (empty($options)) {
      $options = array('filtered_html' => t('Filtered HTML'));
    }

    return $options;
  }

  /**
   * {@inheritdoc}
   */
  public function configFormValidate(&$values) {
    if (!empty($values['author'])) {
      $author = user_load_by_name($values['author']);
      if ($author) {
        $values['author'] = $author->uid;
      }
      else {
        $values['author'] = 0;
        form_set_error('author', t('Username not found.'));
      }
    }
    else {
      $values['author'] = 0;
    }
  }

  /**
   * {@inheritdoc}
   */
  public function setTargetElement(FeedsSource $source, $target_comment, $target_element, $value) {
    $value = is_array($value) ? reset($value) : $value;

    switch ($target_element) {
      case 'nid_by_guid':
        $target_comment->nid = (int) db_select('feeds_item', 'f')
          ->fields('f', array('entity_id'))
          ->condition('entity_type', 'node')
          ->condition('guid', $value)
          ->range(0, 1)
          ->execute()
          ->fetchField();
        break;

      case 'nid_by_title':
        $target_comment->nid = (int) db_select('node', 'n')
          ->fields('n', array('nid'))
          ->condition('title', $value)
          ->range(0, 1)
          ->execute()
          ->fetchField();
        break;

      case 'pid_by_guid':
        $target_comment->pid = (int) db_select('feeds_item', 'f')
          ->fields('f', array('entity_id'))
          ->condition('entity_type', 'comment')
          ->condition('id', $this->id)
          ->condition('feed_nid', $source->feed_nid)
          ->condition('guid', $value)
          ->range(0, 1)
          ->execute()
          ->fetchField();
        break;

      case 'created':
      case 'changed':
        $target_comment->$target_element = feeds_to_unixtime($value, REQUEST_TIME);
        break;

      case 'langcode':
        // Store using the correct Comment class property name.
        $target_comment->langcode = trim($value);
        break;

      case 'author':
        // Store the value as the display name. entityValidate() will attempt
        // to match it against a registered user account via user_load_by_name()
        // and set uid if a match is found. If no match is found it remains as
        // the anonymous display name.
        $value = is_array($value) ? reset($value) : $value;
        $target_comment->name = trim($value);
        break;

      case 'email':
        // Store the email as the display mail for anonymous commenters if uid
        // was not already set in entityValidate() by the 'author' name lookup
        // or attempt to match email to a registered user account.
        $value = is_array($value) ? reset($value) : $value;
        $target_comment->mail = trim($value);
        break;

      case 'hostname':
        // Store the value directly on the comment and also in a staging
        // property so entitySave() can detect that a hostname was explicitly
        // mapped and needs to be written to the database after comment_save().
        // CommentStorageController::preSave() unconditionally overwrites
        // $comment->hostname with the current request IP (a core bug), so
        // we must correct it with a db_update() after comment_save().
        $target_comment->hostname_original = trim($value);
        break;

      case 'cid':
      case 'pid':
      case 'nid':
      case 'uid':
        $target_comment->$target_element = (int) trim($value);
        break;

      case 'thread':
        $target_comment->$target_element = trim($value);
        break;

      default:
        parent::setTargetElement($source, $target_comment, $target_element, $value);
        break;
    }
  }

  /**
   * {@inheritdoc}
   */
  public function getMappingTargets() {
    // parent::getMappingTargets() discovers field-based targets via the entity
    // field API and also invokes hook_feeds_processor_targets_alter(), so call
    // it first then add our own processor-specific targets.
    $targets = parent::getMappingTargets();

    // Explicitly register comment_body so it always appears in the mapping UI
    // regardless of field discovery results.
    $targets['comment_body'] = array(
      'name' => t('Comment body'),
      'description' => t('The main body text of the comment.'),
      'callback' => 'feeds_comment_processor_set_comment_body_target',
    );

    $targets += array(
      'cid' => array(
        'name' => t('Comment ID'),
        'description' => t('The existing comment ID to update. Use with care; comment IDs are normally assigned by Backdrop.'),
      ),
      'pid' => array(
        'name' => t('Parent ID'),
        'description' => t('The comment ID that this comment is a reply to.'),
      ),
      'pid_by_guid' => array(
        'name' => t('Parent ID by GUID'),
        'description' => t('Parent comment looked up by its GUID from a previous import.'),
      ),
      'thread' => array(
        'name' => t('Thread structure'),
        'description' => t('The thread position string for threaded comments.'),
      ),
      'nid' => array(
        'name' => t('Node ID'),
        'description' => t('The ID of the node this comment belongs to.'),
      ),
      'nid_by_guid' => array(
        'name' => t('Node ID by GUID'),
        'description' => t('Node ID looked up by its GUID from a previous import.'),
      ),
      'nid_by_title' => array(
        'name' => t('Node ID by title'),
        'description' => t('Node ID looked up by node title.'),
      ),
      'uid' => array(
        'name' => t('User ID'),
        'description' => t('The user ID of the comment author.'),
      ),
      'subject' => array(
        'name' => t('Subject'),
        'description' => t('The comment subject line.'),
      ),
      'hostname' => array(
        'name' => t('Hostname / IP address'),
        'description' => t("The IP address or hostname of the comment author. When importing from CSV, map this column to preserve the original commenter's IP address."),
      ),
      'created' => array(
        'name' => t('Created date'),
        'description' => t('The date the comment was originally created (Unix timestamp or parseable date string).'),
      ),
      'changed' => array(
        'name' => t('Updated date'),
        'description' => t('The date the comment was last updated.'),
      ),
      'status' => array(
        'name' => t('Published status'),
        'description' => t('0 = unpublished, 1 = published.'),
      ),
      'langcode' => array(
        'name' => t('Language code'),
        'description' => t('The language code of the comment (e.g. "en", "fr"). Defaults to LANGUAGE_NONE (und) if not mapped.'),
      ),
      'author' => array(
        'name' => t('Author'),
        'description' => t("The comment author's name. If the value matches an existing Backdrop username the comment is attributed to that registered user. If no matching account is found the value is stored as the anonymous display name. Map a single author column here for both registered and anonymous commenters."),
      ),
      'email' => array(
        'name' => t('Email'),
        'description' => t("The comment author's email address. If the value matches an existing Backdrop user account the comment is attributed to that user. The email is also stored on the comment for anonymous commenters or notification purposes."),
      ),
      'homepage' => array(
        'name' => t('Homepage'),
        'description' => t("The comment author's website URL."),
      ),
    );

    // Conditionally load Comment Notify integration targets.
    // The comment_notify module is an optional dependency; these targets are
    // only registered when that module is present and enabled.
    if (module_exists('comment_notify')) {
      module_load_include('inc', 'feeds_comment_processor', 'comment_notify_feeds');
      feeds_comment_processor_comment_notify_targets($targets, $this);
    }

    // Invoke alter hooks to wire up the Feeds UI AJAX cogwheel for
    // optional_unique targets including 'guid' from the parent class.
    // This follows the same pattern as FeedsNodeProcessor::getMappingTargets().
    // Without this call, the optional_unique cogwheel does not function for
    // the comment entity type - clicking it has no effect and 'unique' is
    // never saved to the mapping config, causing duplicate comments on
    // re-import. The parent::getMappingTargets() only returns base targets
    // (url, guid) but does NOT call getHookTargets() itself.
    $this->getHookTargets($targets);

    // Ensure all targets have the callback array keys that feeds_ui expects.
    // feeds_ui_mapping_settings_form() iterates these without checking if they
    // exist, causing notices and warnings if they are absent.
    foreach ($targets as &$target) {
      $target += array(
        'summary_callbacks'   => array(),
        'form_callbacks'      => array(),
        'preprocess_callbacks' => array(),
      );
    }
    unset($target);

    return $targets;
  }

  /**
   * Ensure the comment has a valid, truncated subject line.
   *
   * @param stdClass $comment
   *   The comment object to prepare.
   */
  protected function prepareCommentSubject(stdClass $comment) {
    if (!empty($comment->subject)) {
      $comment->subject = truncate_utf8(trim(decode_entities(strip_tags($comment->subject))), 64, TRUE);
    }

    if (empty($comment->subject)) {
      // Generate from the comment body if available.
      if (!empty($comment->comment_body)) {
        $langcode = $comment->langcode ?? LANGUAGE_NONE;
        $body_value = $comment->comment_body[$langcode][0]['value'] ?? '';
        $comment->subject = truncate_utf8(trim(decode_entities(strip_tags($body_value))), 29, TRUE);
      }

      if (empty($comment->subject)) {
        $comment->subject = t('(No subject)');
      }
    }
  }

  /**
   * Calculate the thread value for a new threaded comment.
   *
   * @param stdClass $comment
   *   The comment for which to calculate a thread value.
   *
   * @return string
   *   The thread value (vancode-based, trailing slash).
   */
  protected function calculateThread(stdClass $comment) {
    if (empty($comment->pid)) {
      // Top-level comment: find the current maximum and increment.
      $max = db_query('SELECT MAX(thread) FROM {comment} WHERE nid = :nid', array(':nid' => $comment->nid))->fetchField();
      $max = rtrim((string) $max, '/');
      $parts = explode('.', $max);
      $first = $parts[0];
      return int2vancode(vancode2int($first) + 1) . '/';
    }

    // Threaded reply: increment the deepest segment under the parent thread.
    $parent = comment_load($comment->pid);
    $parent->thread = rtrim((string) $parent->thread, '/');

    $max = db_query("SELECT MAX(thread) FROM {comment} WHERE thread LIKE :thread AND nid = :nid", array(
      ':thread' => $parent->thread . '.%',
      ':nid'    => $comment->nid,
    ))->fetchField();

    if ($max == '') {
      // First child of this parent.
      return $parent->thread . '.' . int2vancode(0) . '/';
    }

    $max = rtrim($max, '/');
    $parts = explode('.', $max);
    $parent_depth = count(explode('.', $parent->thread));
    $last = $parts[$parent_depth];
    return $parent->thread . '.' . int2vancode(vancode2int($last) + 1) . '/';
  }

}
