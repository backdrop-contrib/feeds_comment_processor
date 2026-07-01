<?php
/**
 * @file
 * Comment Notify integration for Feeds Comment Processor.
 *
 * This file is only loaded when the comment_notify module is enabled. It is
 * never listed as a dependency in feeds_comment_processor.info and is loaded
 * conditionally via module_load_include() inside getMappingTargets() and
 * entitySave(). Sites that do not use comment_notify are completely unaffected.
 */

/**
 * Adds Comment Notify mapping targets to the Feeds comment processor.
 *
 * Called from FeedsCommentProcessor::getMappingTargets() only when the
 * comment_notify module is present.
 *
 * @param array $targets
 *   The existing array of mapping targets, passed by reference.
 * @param FeedsCommentProcessor $processor
 *   The processor instance (available for any configuration lookups).
 */
function feeds_comment_processor_comment_notify_targets(array &$targets, FeedsCommentProcessor $processor) {
  $targets['comment_notify'] = array(
    'name'        => t('Comment Notify: Subscription type'),
    'description' => t('Sets the email subscription status for the imported comment. Matches the {comment_notify}.notify column values: 0 (COMMENT_NOTIFY_DISABLED) = no notifications, 1 (COMMENT_NOTIFY_NODE) = all comments on this node, 2 (COMMENT_NOTIFY_COMMENT) = replies to this comment only.'),
    'callback'    => 'feeds_comment_processor_set_comment_notify_target',
  );
}

/**
 * Feeds mapping callback to set the comment notification subscription type.
 *
 * Follows the Feeds callback ($source, $entity, $target, $value, $mapping)
 *
 * The value is stored as a temporary property on the comment object and is
 * written to the {comment_notify} table in entitySave() after the comment has
 * been saved and has a cid.
 *
 * @param FeedsSource $source
 *   The feeds source.
 * @param object $entity
 *   The comment entity being built.
 * @param string $target
 *   The target name ('comment_notify').
 * @param mixed $value
 *   The subscription type value (0, 1, or 2).
 * @param array $mapping
 *   The full mapping configuration array.
 */
function feeds_comment_processor_set_comment_notify_target(FeedsSource $source, $entity, $target, $value, array $mapping) {
  // Feeds passes values as single-element arrays; extract the scalar first.
  // Casting an array directly to int always returns 1 regardless of the value.
  $value = is_array($value) ? reset($value) : $value;
  $notify = (int) $value;
  if (!in_array($notify, array(0, 1, 2))) {
    $notify = 0;
  }
  // Store on the entity as a temporary staging property; it is not a real
  // comment field. FeedsCommentProcessor::entitySave() reads this before
  // comment_save() and sets $comment->notify so hook_comment_insert() writes
  // the correct subscription type to {comment_notify}.
  $entity->comment_notify_value = $notify;
}
