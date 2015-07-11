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
   * Creates a new comment in memory and returns it.
   */
  protected function newEntity(FeedsSource $source) {
    $comment = new stdClass();
    $comment->cid = 0;
    $comment->pid = 0;
    $comment->uid = $this->config['author'];
    $account = user_load($comment->uid);
    $comment->mail = $account->mail;
    $comment->name = $account->name;
    $comment->node_type = !empty($this->config['bundle']) ? $this->config['bundle'] : NULL;
    $comment->hostname = '127.0.0.1';
    $comment->created = FEEDS_REQUEST_TIME;
    $comment->date = FEEDS_REQUEST_TIME;
    $comment->is_anonymous = 0;
    $comment->status = 0;
    $comment->language = LANGUAGE_NONE;
    $comment->comment_body[$comment->language][0]['format'] = $this->config['input_format'];

    return $comment;
  }

  /**
   * Loads an existing comment.
   */
  protected function entityLoad(FeedsSource $source, $cid) {
    return comment_load($cid);
  }

  /**
   * Validates a comment.
   */
  protected function entityValidate($comment) {
    if (empty($comment->nid)) {
      throw new FeedsValidationException(t('Unable to create comment with empty NID.'));
    }
  }

  /**
   * Save a comment.
   */
  public function entitySave($comment) {
    $comment->date = date('r', $comment->created);
    // Trim the subject if it is too long.
    $comment->subject = truncate_utf8(strip_tags($comment->subject), 64, TRUE, TRUE);
    comment_submit($comment);
    comment_save($comment);
  }

  /**
   * Delete multiple comments.
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
    $defaults['input_format'] = filter_default_format();
    $defaults['expire'] = FEEDS_EXPIRE_NEVER;
    $defaults['author'] = 0;
    return $defaults;
  }

  /**
   * Override parent::configForm().
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
    $period = drupal_map_assoc(array(FEEDS_EXPIRE_NEVER, 3600, 10800, 21600, 43200, 86400, 259200, 604800, 604800 * 4, 604800 * 12, 604800 * 24, 31536000), 'feeds_format_expire');
    $form['expire'] = array(
      '#type' => 'select',
      '#title' => t('Expire comments'),
      '#options' => $period,
      '#description' => t('Select after how much time comments should be deleted. The comment\'s published date will be used for determining the comment\'s age, see Mapping settings.'),
      '#default_value' => $this->config['expire'],
    );

    return $form;
  }

  /**
   * Override parent::configFormValidate().
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
   * Override setTargetElement to operate on a target item that is a comment.
   */
  public function setTargetElement(FeedsSource $source, $target_comment, $target_element, $value) {
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

      default:
        parent::setTargetElement($source, $target_comment, $target_element, $value);
        break;
    }
  }

  /**
   * Return available mapping targets.
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
        'description' => t('The author\'s host name.'),
      ),
      'created' => array(
        'name' => t('Published date'),
        'description' => t('The UNIX time when a comment has been saved.'),
      ),
      'status' => array(
        'name' => t('Published status'),
        'description' => t('The published status of a comment. (0 = Not Published, 1 = Published)'),
      ),
      'name' => array(
        'name' => t('Name'),
        'description' => t('The comment author\'s name.'),
      ),
      'mail' => array(
        'name' => t('Email'),
        'description' => t('The comment author\'s e-mail address.'),
      ),
      'homepage' => array(
        'name' => t('Homepage'),
        'description' => t('The comment author\'s home page address'),
      ),
    ) + parent::getMappingTargets();

    // Let other modules expose mapping targets.
    self::loadMappers();
    feeds_alter('feeds_comment_processor_targets', $targets);

    return $targets;
  }

}
