<?php
/**
 * @file
 * Contains FeedsCommentProcessor.
 *
 * Backdrop's comment constants:
 * These are defined in comment.module
 * COMMENT_NODE_OPEN = 1 - comments allowed
 * COMMENT_NODE_CLOSED = 0 - closed to new comments
 * COMMENT_NODE_DISABLED = 2 - comments completely disabled
 */

/**
 * Creates comments from feed items.
 */
class FeedsCommentProcessor extends FeedsProcessor {

  /**
   * Override parent::bundleRequired().
   */
  public function bundleRequired() {
    return FALSE;
  }

  /**
   * Override parent::bundleKey().
   */
  public function bundleKey() {
    return 'node_type';
  }

  /**
   * Override parent::bundle().
   */
  public function bundle() {
    return isset($this->config['node_type']) ? $this->config['node_type'] : 'page';
  }

  /**
   * Define entity type.
   */
  public function entityType() {
    return 'comment';
  }

  /**
   * Implements parent::entityInfo().
   */
  protected function entityInfo() {
    $info = parent::entityInfo();
    $info['label plural'] = t('Comments');
    return $info;
  }

  /**
   * Creates a new comment in memory and return it.
   */
  protected function newEntity(FeedsSource $source) {
    $comment = new stdClass();
    $comment->cid = 0;
    $comment->pid = 0;
    $comment->nid = 0;
    $comment->uid = $this->config['author'];

    // CRITICAL: Set node_type to a valid content type that exists on your site.
    // This satisfies Backdrop's bundle requirement.
    // The value will be overridden when nid is mapped during import.
    $comment->node_type = 'page';

    $comment->hostname = '127.0.0.1';
    $comment->created = REQUEST_TIME;
    $comment->changed = REQUEST_TIME;
    $comment->language = LANGUAGE_NONE;
    $comment->name = '';
    $comment->mail = '';
    $comment->subject = '';
    $comment->feeds_is_new = TRUE;

    // Mark as new for Feeds tracking.
    $comment->feeds_is_new = TRUE;

    return $comment;
  }

  /**
   * Validates a comment before saving.
   */
  protected function entityValidate($entity, FeedsSource $source = NULL) {
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

    // Check if comments are allowed on this content type.
    $node_type = node_type_get_type($node->type);
    $comment_setting = isset($node_type->settings['comment_enabled']) ? $node_type->settings['comment_enabled'] : COMMENT_NODE_OPEN;

    if ($comment_setting == COMMENT_NODE_DISABLED) {
      throw new FeedsValidationException(t('Cannot create comment: Comments are disabled for content type "@type".', array('@type' => $node_type->name)));
    }

    // Check if this specific node allows comments.
    if (isset($node->comment) && $node->comment == COMMENT_NODE_DISABLED) {
      throw new FeedsValidationException(t('Cannot create comment: Comments are disabled for node @nid.', array('@nid' => $comment->nid)));
    }

    // Validate parent comment if provided.
    if ($comment->pid && !comment_load($comment->pid)) {
      throw new FeedsValidationException(t('Invalid parent comment id.'));
    }

    // Set default status.
    $comment->status = empty($comment->status) ? COMMENT_NOT_PUBLISHED : COMMENT_PUBLISHED;

    // Prepare the subject.
    $this->prepareCommentSubject($comment);

    // Validate email if provided.
    if ($comment->mail && !valid_email_address($comment->mail)) {
      $comment->mail = '';
    }

    // Handle author permissions.
    if ($comment->uid && $account = user_load($comment->uid)) {
      $comment->name = $comment->name ?: $account->name;
      $comment->mail = $comment->mail ?: $account->mail;

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
   * Check that the user has permission to save a comment.
   */
  protected function entitySaveAccess($entity, FeedsSource $source = NULL) {
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
   * Save a comment.
   */
  public function entitySave($entity, FeedsSource $source = NULL) {
    $comment = $entity;

    // Handle existing comment update.
    if (!empty($comment->feeds_is_new) && $comment->cid) {
      if (empty($comment->thread)) {
        $comment->thread = $this->calculateThread($comment);
      }
      backdrop_write_record('comment', $comment);
    }

    // Temporarily override IP address if hostname is mapped.
    $prev_ip = ip_address();
    $ip_address = &backdrop_static('ip_address');
    if (!empty($comment->hostname)) {
      $ip_address = $comment->hostname;
    }

    comment_save($comment);

    // Restore original IP.
    $ip_address = $prev_ip;
  }

  /**
   * Delete multiple comments.
   */
  protected function entityDeleteMultiple($entity_ids, FeedsSource $source = NULL) {
    comment_delete_multiple($entity_ids);
  }

  /**
   * Query for expired comments.
   */
  protected function expiryQuery(FeedsSource $source, $time) {
    $select = parent::expiryQuery($source, $time);
    $select->condition('e.created', REQUEST_TIME - $time, '<');
    return $select;
  }

  /**
   * Return expiry time.
   */
  public function expiryTime() {
    return $this->config['expire'];
  }

  /**
   * Override parent::configDefaults().
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
   * Override parent::configForm().
   */
  public function configForm(&$form_state) {
    $form = parent::configForm($form_state);

    // Remove the default bundle selector that parent class adds automatically.
    unset($form['bundle']);

    // Safely get text formats - cache them to avoid recursion.
    $formats = $this->getAvailableTextFormats();

    $form['input_format'] = array(
      '#type' => 'select',
      '#title' => t('Comment input format'),
      '#description' => t('Text format for imported comments.'),
      '#options' => $formats,
      '#default_value' => isset($this->config['input_format']) ? $this->config['input_format'] : 'filtered_html',
    );

    // Author selection.
    $author = user_load($this->config['author']);
    $form['author'] = array(
      '#type' => 'textfield',
      '#title' => t('Default author'),
      '#description' => t('Username of the comment author. Can be overridden by mapping user_name or user_mail.'),
      '#autocomplete_path' => 'user/autocomplete',
      '#default_value' => empty($author->name) ? '' : check_plain($author->name),
    );

    $form['authorize'] = array(
      '#type' => 'checkbox',
      '#title' => t('Check author permissions'),
      '#description' => t('Verify the author has permission to post comments.'),
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
      '#description' => t("Delete comments after this time. Uses the comment's created date."),
      '#default_value' => $this->config['expire'],
    );

    return $form;
  }

  /**
   * Helper function to safely get available text formats.
   * Caches the result to avoid recursion and handles missing filter module.
   */
  protected function getAvailableTextFormats() {
    // Static cache to prevent multiple calls.
    static $formats = array();

    if (!empty($formats)) {
      return $formats;
    }

    // Check if filter module is enabled.
    if (!module_exists('filter')) {
      $formats = array('filtered_html' => t('Filtered HTML'));
      return $formats;
    }

    // Load all text formats safely.
    $formats = filter_formats();

    // Convert to simple array if needed.
    $options = array();
    foreach ($formats as $format) {
      $options[$format->format] = $format->name;
    }

    // Ensure we have at least one format.
    if (empty($options)) {
      $options = array('filtered_html' => t('Filtered HTML'));
    }

    return $options;
  }

  /**
   * Validate the processor configuration form.
   */
  public function configFormValidate(&$values) {
    if (!empty($values['author'])) {
      $author = user_load_by_name($values['author']);
      if ($author) {
        $values['author'] = $author->uid;
      } else {
        $values['author'] = 0;
        form_set_error('author', t('Username not found.'));
      }
    } else {
      $values['author'] = 0;
    }
  }

  /**
   * Set values on the comment entity from mapped sources.
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

      case 'user_name':
        if ($account = user_load_by_name($value)) {
          $target_comment->uid = $account->uid;
        }
        break;

      case 'user_mail':
        if ($account = user_load_by_mail($value)) {
          $target_comment->uid = $account->uid;
        }
        break;

      case 'cid':
      case 'pid':
      case 'nid':
      case 'uid':
        $target_comment->$target_element = (int) trim($value);
        break;

      case 'name':
      case 'mail':
      case 'hostname':
      case 'thread':
        $target_comment->$target_element = trim($value);
        break;

      default:
        parent::setTargetElement($source, $target_comment, $target_element, $value);
        break;
    }
  }

  /**
   * Return available mapping targets.
   */
  public function getMappingTargets() {
    // First, get all targets from parent class (includes field-based targets).
    $targets = parent::getMappingTargets();

    // Explicitly add comment_body as a target.
    // This ensures it appears regardless of field discovery issues.
    $targets['comment_body'] = array(
      'name' => t('Comment body'),
      'description' => t('The main content of the comment.'),
      'callback' => 'feeds_comment_processor_set_comment_body_target',
    );

    // Then add your custom processor-specific targets.
    $targets += array(
      'cid' => array(
        'name' => t('Comment ID'),
        'description' => t('The existing comment ID to update. Use with care, comment ids are usually assigned by Backdrop.'),
      ),
      'pid' => array(
        'name' => t('Parent ID'),
        'description' => t('The comment ID that this comment replies to.'),
      ),
      'pid_by_guid' => array(
        'name' => t('Parent ID by GUID'),
        'description' => t('Parent comment looked up by GUID from previous import.'),
      ),
      'thread' => array(
        'name' => t('Thread structure'),
        'description' => t('The thread position for threaded comments.'),
      ),
      'nid' => array(
        'name' => t('Node ID'),
        'description' => t('The node ID that this comment belongs to.'),
      ),
      'nid_by_guid' => array(
        'name' => t('Node ID by GUID'),
        'description' => t('Node ID looked up by GUID from previous import.'),
      ),
      'nid_by_title' => array(
        'name' => t('Node ID by title'),
        'description' => t('Node ID looked up by node title.'),
      ),
      'uid' => array(
        'name' => t('User ID'),
        'description' => t('The user ID of the comment author.'),
      ),
      'user_name' => array(
        'name' => t('Username'),
        'description' => t('The username of the comment author.'),
      ),
      'user_mail' => array(
        'name' => t('User email'),
        'description' => t('The email address of the comment author.'),
      ),
      'subject' => array(
        'name' => t('Subject'),
        'description' => t('The comment subject line.'),
      ),
      'hostname' => array(
        'name' => t('Hostname'),
        'description' => t("The author's IP address or hostname."),
      ),
      'created' => array(
        'name' => t('Created date'),
        'description' => t('When the comment was created.'),
      ),
      'changed' => array(
        'name' => t('Updated date'),
        'description' => t('When the comment was last updated.'),
      ),
      'status' => array(
        'name' => t('Published status'),
        'description' => t('0 = unpublished, 1 = published.'),
      ),
      'name' => array(
        'name' => t('Name'),
        'description' => t("The comment author's displayed name."),
      ),
      'mail' => array(
        'name' => t('Email'),
        'description' => t("The comment author's email address."),
      ),
      'homepage' => array(
        'name' => t('Homepage'),
        'description' => t("The comment author's website URL."),
      ),
    );

    // Allow other modules to add targets.
    $this->getHookTargets($targets);

    return $targets;
  }

  /**
   * Ensure the comment has a valid subject.
   */
  protected function prepareCommentSubject(stdClass $comment) {
    if (!empty($comment->subject)) {
      $comment->subject = truncate_utf8(trim(decode_entities(strip_tags($comment->subject))), 64, TRUE);
    }

    if (empty($comment->subject)) {
      // Generate subject from comment body.
      if (!empty($comment->comment_body)) {
        $langcode = isset($comment->language) ? $comment->language : LANGUAGE_NONE;
        $body_value = isset($comment->comment_body[$langcode][0]['value']) ? $comment->comment_body[$langcode][0]['value'] : '';
        $comment->subject = truncate_utf8(trim(decode_entities(strip_tags($body_value))), 29, TRUE);
      }

      // Final fallback.
      if (empty($comment->subject)) {
        $comment->subject = t('(No subject)');
      }
    }
  }

  /**
   * Calculate the thread value for threaded comments.
   */
  protected function calculateThread(stdClass $comment) {
    if ($comment->pid == 0) {
      // This is a comment with no parent comment (depth 0).
      // Start by retrieving the maximum thread value.
      $max = db_query('SELECT MAX(thread) FROM {comment} WHERE nid = :nid', array(':nid' => $comment->nid))->fetchField();
      // Strip the "/" from the end of the thread.
      $max = rtrim($max, '/');
      // Get the value at the correct depth.
      $parts = explode('.', $max);
      $firstsegment = $parts[0];
      // Finally, build the thread field for this new comment.
      return int2vancode(vancode2int($firstsegment) + 1) . '/';
    }

    // This is a comment with a parent comment, so increase the part of the
    // thread value at the proper depth.
    // Get the parent comment:
    $parent = comment_load($comment->pid);
    // Strip the "/" from the end of the parent thread.
    $parent->thread = rtrim((string) $parent->thread, '/');
    // Get the max value in *this* thread.
    $max = db_query("SELECT MAX(thread) FROM {comment} WHERE thread LIKE :thread AND nid = :nid", array(
      ':thread' => $parent->thread . '.%',
      ':nid' => $comment->nid,
    ))->fetchField();

    // First child of this parent.
    if ($max == '') {
      return $parent->thread . '.' . int2vancode(0) . '/';
    }

    // Strip the "/" at the end of the thread.
    $max = rtrim($max, '/');
    // Get the value at the correct depth.
    $parts = explode('.', $max);
    $parent_depth = count(explode('.', $parent->thread));
    $last = $parts[$parent_depth];
    // Finally, build the thread field for this new comment.
    return $parent->thread . '.' . int2vancode(vancode2int($last) + 1) . '/';
  }
}
