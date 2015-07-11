<?php

/**
 * @file
 * Contains \Drupal\feeds_comment_processor\Tests\ProcessorWebTest.
 */

namespace Drupal\feeds_comment_processor\Tests;

/**
 * Basic behavior tests for feeds_comment_processor.
 */
class ProcessorWebTest extends \FeedsWebTestCase {

  public static function getInfo() {
    return array(
      'name' => 'Tests for comment processor',
      'description' => 'Tests that comments can be imported.',
      'group' => 'Feeds comment processor',
    );
  }

  /**
   * {@inheritdoc}
   */
  public function setUp() {
    parent::setUp('comment', 'feeds_comment_processor');

    // Create an importer configuration.
    $this->createImporterConfiguration('Test', 'comment');

    // Set and configure plugins.
    $this->setPlugin('comment', 'FeedsCSVParser');
    $this->setPlugin('comment', 'FeedsCommentProcessor');

    $edit = array(
      'bundle' => 'comment_node_article',
    );
    $this->setSettings('comment', 'FeedsCommentProcessor', $edit);

    $this->addMappings('comment',
      array(
        0 => array(
          'source' => 'subject',
          'target' => 'subject',
        ),
        1 => array(
          'source' => 'guid',
          'target' => 'nid_by_guid',
        ),
      )
    );
  }

  /**
   * Tests a very basic import.
   */
  public function test() {
    $parent = (object) array('title' => 'Parent', 'type' => 'article');
    node_save($parent);

    // Insert a feeds_item record.
    $item = (object) array(
      'guid' => 1,
      'url' => '',
      'entity_type' => 'node',
      'entity_id' => $parent->nid,
      'feed_nid' => 0,
      'id' => 'node_importer',
    );
    drupal_write_record('feeds_item', $item);

    $url = $GLOBALS['base_url'] . '/' . drupal_get_path('module', 'feeds_comment_processor') . '/tests/test.csv';
    $nid = $this->createFeedNode('comment', $url, 'Comment test');

    $this->assertText('Created 1 comment');

    $this->assertEqual(1, db_query("SELECT COUNT(*) FROM {comment}")->fetchField());
  }

}
