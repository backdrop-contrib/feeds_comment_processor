<?php

/**
 * @file
 * Contains FeedsCommentProcessor.
 */

/**
 * Creates comments from feed items.
 */
class FeedsCommentProcessor extends FeedsProcessor {

  /**
   * {@inheritdoc}
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
   */
  protected function newEntity(FeedsSource $source) {
    $comment = new stdClass();
    $comment->cid = 0;
    $comment->pid = 0;
    $comment->uid = $this->config['author'];
    $comment->node_type = !empty($this->config['bundle']) ? $this->config['bundle'] : NULL;
    $comment->hostname = '127.0.0.1';
    $comment->created = REQUEST_TIME;
    $comment->changed = REQUEST_TIME;
    $comment->language = LANGUAGE_NONE;
    $comment->name = '';
    $comment->mail = '';
    $comment->subject = '';
    $comment->comment_body[$comment->language][0]['format'] = $this->config['input_format'];

    return $comment;
  }

  /**
   * {@inheritdoc}
   */
  protected function entityLoad(FeedsSource $source, $cid) {
    return comment_load($cid);
  }

  /**
   * {@inheritdoc}
   */
  protected function entityValidate($comment) {
    if (empty($comment->nid)) {
      throw new FeedsValidationException(t('Unable to create comment with empty NID.'));
    }

    $comment->status = empty($comment->status) ? COMMENT_NOT_PUBLISHED : COMMENT_PUBLISHED;

    $this->prepareCommentSubject($comment);

    foreach (array('uid', 'name', 'mail', 'hostname') as $key) {
      $comment->$key = trim($comment->$key);
    }

    $comment->uid = (int) $comment->uid;

    $comment->mail = valid_email_address($comment->mail) ? $comment->mail : '';

    // If there is a valid user id, populate any default values.
    if ($comment->uid && $account = user_load($comment->uid)) {
      $comment->name = $comment->name ? $comment->name : $account->name;
      $comment->mail = $comment->mail ? $comment->mail : $account->mail;

      // Unpublish comments by users that are not allowed to skip approval.
      if (!user_access('skip comment approval', $account)) {
        $comment->status = COMMENT_NOT_PUBLISHED;
      }
    }
    elseif (!$comment->uid && !$comment->name) {
      $comment->name = variable_get('anonymous', t('Anonymous'));
    }
  }

  /**
   * {@inheritdoc}
   */
  protected function entitySaveAccess($comment) {
    if (!$this->config['authorize'] || !$comment->uid) {
      return;
    }

    if (!$author = user_load($comment->uid)) {
      throw new FeedsAccessException(t('User %uid is not a valid user.', array('%uid' => $comment->uid)));
    }

    if (!user_access('post comments', $author)) {
      throw new FeedsAccessException(t('User %user is not permitted to post comments.', array('%user' => $author->name)));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function entitySave($comment) {
    // Trick ip_address() to return the ip address mapped to the comment.
    $prev_ip = ip_address();
    $ip_address = &drupal_static('ip_address');

    // Set the ip address to the comment hostname.
    $ip_address = $comment->hostname;
    comment_save($comment);

    // Restore the previous ip.
    $ip_address = $prev_ip;
  }

  /**
   * {@inheritdoc}
   */
  protected function entityDeleteMultiple($cids) {
    comment_delete_multiple($cids);
  }

  /**
   * {@inheritdoc}
   */
  public function expire(FeedsSource $source, $time = NULL) {
    $state = $source->state(FEEDS_PROCESS_EXPIRE);

    if ($time === NULL) {
      $time = $this->expiryTime();
    }
    if ($time == FEEDS_EXPIRE_NEVER) {
      return;
    }

    $select = $this->expiryQuery($source, $time);

    // If there is no total, query it.
    if (!$state->total) {
      $state->total = $select->countQuery()->execute()->fetchField();
    }

    // Delete a batch of entities.
    $entity_ids = $select->range(0, $this->getLimit())->execute()->fetchCol();
    if ($entity_ids) {
      $this->entityDeleteMultiple($entity_ids);
      $state->deleted += count($entity_ids);
      $state->progress($state->total, $state->deleted);
    }
    else {
      $state->progress($state->total, $state->total);
    }
  }

  /**
   * {@inheritdoc}
   */
  protected function expiryQuery(FeedsSource $source, $time) {
    // Build base select statement.
    $select = db_select('comment', 'c');
    $select->addField('c', 'cid');
    $select->condition('c.created', REQUEST_TIME - $time, '<');

    $select->join('feeds_item', 'fi', 'c.cid = fi.entity_id');

    $select->condition('fi.entity_type', 'comment');
    $select->condition('fi.id', $this->id);
    $select->condition('fi.feed_nid', $source->feed_nid);

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
    $defaults['input_format'] = filter_default_format();
    $defaults['expire'] = FEEDS_EXPIRE_NEVER;
    $defaults['author'] = 0;
    $defaults['authorize'] = TRUE;

    return $defaults;
  }

  /**
   * {@inheritdoc}
   */
  public function configForm(&$form_state) {
    $form = parent::configForm($form_state);

    $form['bundle']['#title'] = t('Comment type');

    $author = user_load($this->config['author']);
    $form['author'] = array(
      '#type' => 'textfield',
      '#title' => t('Author'),
      '#description' => t('Select the author of the comments to be created - leave empty to assign "anonymous".'),
      '#autocomplete_path' => 'user/autocomplete',
      '#default_value' => empty($author->name) ?  'anonymous' : check_plain($author->name),
    );

    $form['authorize'] = array(
      '#type' => 'checkbox',
      '#title' => t('Authorize'),
      '#description' => t('Check that the author has permission to create and publish comments.'),
      '#default_value' => $this->config['authorize'],
    );

    $period = drupal_map_assoc(array(FEEDS_EXPIRE_NEVER, 3600, 10800, 21600, 43200, 86400, 259200, 604800, 604800 * 4, 604800 * 12, 604800 * 24, 31536000), 'feeds_format_expire');
    $form['expire'] = array(
      '#type' => 'select',
      '#title' => t('Expire comments'),
      '#options' => $period,
      '#description' => t("Select after how much time comments should be deleted. The comment's published date will be used for determining the comment's age, see Mapping settings."),
      '#default_value' => $this->config['expire'],
    );

    return $form;
  }

  /**
   * {@inheritdoc}
   */
  public function configFormValidate(&$values) {
    if ($author = user_load_by_name($values['author'])) {
      $values['author'] = $author->uid;
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
        $target_comment->nid = db_select('feeds_item', 'f')
          ->fields('f', array('entity_id'))
          ->condition('entity_type', 'node')
          ->condition('guid', $value)
          ->execute()
          ->fetchField();
        break;

      case 'comment':
        $target_comment->comment_body[$target_comment->language][0]['value'] = $value;
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

      default:
        parent::setTargetElement($source, $target_comment, $target_element, $value);
        break;
    }
  }

  /**
   * {@inheritdoc}
   */
  public function getMappingTargets() {
    $targets = array(
      'pid' => array(
        'name' => t('Parent ID'),
        'description' => t('The cid to which this comment is a reply.'),
      ),
      'nid' => array(
        'name' => t('Node ID'),
        'description' => t('The nid to which this comment is a reply.'),
      ),
      'nid_by_guid' => array(
        'name' => t('Node ID by GUID'),
        'description' => t('The node to which this comment is a reply, looked up by the GUID of a previous import.'),
      ),
      'uid' => array(
        'name' => t('User ID'),
        'description' => t('The Drupal user ID of the comment author.'),
      ),
      'user_name' => array(
        'name' => t('Username'),
        'description' => t('The Drupal username of the comment author.'),
      ),
      'user_mail' => array(
        'name' => t('User email'),
        'description' => t('The email address of the comment author.'),
      ),
      'subject' => array(
        'name' => t('Title'),
        'description' => t('The title of the comment.'),
      ),
      'comment' => array(
        'name' => t('Comment'),
        'description' => t('The comment body.'),
      ),
      'hostname' => array(
        'name' => t('Hostname'),
        'description' => t("The author's host name."),
      ),
      'created' => array(
        'name' => t('Published date'),
        'description' => t('The time when a comment was saved.'),
      ),
      'changed' => array(
        'name' => t('Updated date'),
        'description' => t('The time when a comment was updated.'),
      ),
      'status' => array(
        'name' => t('Published status'),
        'description' => t('The published status of a comment. (0 = Not Published, 1 = Published)'),
      ),
      'name' => array(
        'name' => t('Name'),
        'description' => t("The comment author's name."),
      ),
      'mail' => array(
        'name' => t('Email'),
        'description' => t("The comment author's e-mail address."),
      ),
      'homepage' => array(
        'name' => t('Homepage'),
        'description' => t("The comment author's home page address"),
      ),
    ) + parent::getMappingTargets();

    // @todo Remove this at some point.
    drupal_alter('feeds_comment_processor_targets', $targets);

    // Detect modern version of Feeds.
    if (method_exists($this, 'getHookTargets')) {
      $this->getHookTargets($targets);
    }
    else {
      self::loadMappers();
      $entity_type = $this->entityType();
      $bundle = $this->bundle();

      drupal_alter('feeds_processor_targets', $targets, $entity_type, $bundle);
    }

    return $targets;
  }

  /**
   * Ensures that a comment subject exists and is valid.
   *
   * @param stdClass $comment
   *   The comment.
   */
  protected function prepareCommentSubject(stdClass $comment) {
    $comment->subject = truncate_utf8(trim(decode_entities(strip_tags($comment->subject))), 64, TRUE);

    if ($comment->subject !== '') {
      return;
    }

    // The body may be in any format, so:
    // 1) Filter it into HTML
    // 2) Strip out all HTML tags
    // 3) Convert entities back to plain-text.
    $field = field_info_field('comment_body');
    $langcode = field_is_translatable('comment', $field) ? entity_language('comment', $comment) : LANGUAGE_NONE;
    $comment_body = $comment->comment_body[$langcode][0];
    if (isset($comment_body['format'])) {
      $comment_text = check_markup($comment_body['value'], $comment_body['format']);
    }
    else {
      $comment_text = check_plain($comment_body['value']);
    }
    $comment->subject = truncate_utf8(trim(decode_entities(strip_tags($comment_text))), 29, TRUE);
    // Edge cases where the comment body is populated only by HTML tags will
    // require a default subject.
    if ($comment->subject === '') {
      $comment->subject = t('(No subject)');
    }
  }

}
