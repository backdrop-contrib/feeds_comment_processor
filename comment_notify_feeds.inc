<?php
/**
 * @file
 * Comment Notify integration for Feeds Comment Processor.
 */

/**
 * Adds Comment Notify specific targets to the Feeds processor.
 *
 * @param array &$targets
 *   The existing array of mapping targets.
 * @param FeedsCommentProcessor $processor
 *   The processor instance.
 */
function feeds_comment_processor_comment_notify_targets(&$targets, $processor) {
  $targets['comment_notify'] = array(
    'name' => t('Comment Notify: Subscription type'),
    'description' => t('0 = no notifications, 1 = all comments, 2 = replies only'),
    'callback' => 'feeds_comment_processor_set_comment_notify_target',
  );
}

/**
 * Callback to set comment notification subscription type.
 */
function feeds_comment_processor_set_comment_notify_target($comment, $target_name, $value, $source, $importer, $processor) {
  $notify = (int) $value;
  if (!in_array($notify, array(0, 1, 2))) {
    $notify = 0;
  }
  $comment->comment_notify_value = $notify;
}
