<?php

/**
 * @file
 * Feeds hooks.
 */

/**
 * Implements hook_feeds_plugins().
 */
function feeds_comment_processor_feeds_plugins() {
  $info = array();

  $info['FeedsCommentProcessor'] = array(
    'name' => 'Comment processor',
    'description' => 'Create and update comments.',
    'help' => 'Create and update comments from parsed content.',
    'module' => 'feeds_comment_processor',
    'handler' => array(
      'parent' => 'FeedsProcessor',
      'class' => 'FeedsCommentProcessor',
      'file' => 'FeedsCommentProcessor.inc',
      'path' => backdrop_get_path('module', 'feeds_comment_processor'),
    ),
  );

  return $info;
}
