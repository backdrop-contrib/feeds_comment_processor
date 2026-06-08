# Feeds Comment Processor

## Description

Feeds Comment Processor provides support for creating and updating comments with the Feeds module. The Feeds module provides support for node, user, and taxonomy term imports, but does not include support for comments.

**NOTE: This port is in an early state and needs testing. Use on development server and ensure your data is backed up.**

## Dependencies

- Comments
- [Feeds](https://backdropcms.org/project/feeds)

## Installation

Install this module using the official Backdrop CMS instructions at https://backdropcms.org/guide/modules.

## Configuration and use

After enabling this module:
1. Add importer.
Go to Feeds importers at admin/structure/feeds and click "Add importer".
For example: Name = Comment import; Description = Comments import from CSV file.
2. Under Processor, select Comment processor.
3. Review Comment processor settings and configure as required.
4. Go to Comment processor mapping and configure source and targets, see below.
5. Configure other settings as required.

## Comment mapping

The Comment ID (CID) in the source is generally not mapped to the CID in the
target except when importing to existing comments. When importing new comments
let Backdrop assign the CID with automatic incrementing.

However, the source CID can be mapped to GUID, and if the source includes the
Parent CID for threaded comments this can be mapped to Parent ID by GUID. As
long as child comments are in a later row and the Parent CID is empty when not
used Backdrop automatically calculates the correct thread position.

CSV column -> Maps to target
CID -> GUID
Parent CID -> Parent ID by GUID
NID -> Node ID
Comment Title -> Subject
Body -> Comment body
etc. -> etc.

## Advanced usage

### Thread structure (thread)

The **Thread structure (thread)** target is available for site builders who need
to import pre-calculated thread position strings from another system.

It is not needed for normal CSV imports, as described above.

### What is a thread position?

In threaded comment systems, each comment needs a position code (e.g. `01/`, `01.01/`, `01.02/`) that determines where it appears in the conversation hierarchy. This is stored in the `comment.thread` database column.

### When to use it

- ✅ **Use this target** when migrating comments from another Drupal/Backdrop site and you need to preserve the exact comment ordering.
- ❌ **Do NOT use** for normal CSV imports. Backdrop automatically calculates the correct thread position from the `pid` (parent comment ID) you import.

### Warning

Importing an incorrect or conflicting `thread` value can break the comment display order for an entire node. Only use this target if you:
1. Know exactly what the thread values represent
2. Are migrating from a system that uses the same threading algorithm

**If you don't understand what this is, leave it unmapped.**

## Future plans

Plans include adding integration for the [Comment Notify](https://backdropcms.org/project/comment_notify) module.

The comment_notify integration takes the approach of mapping the notify options
and using those to set the notified value assuming that where notifications are
enabled they were already notified by the source system and the notified value
is set to 1. This avoids the destination system resending email.

## Documentation

Additional documentation is located in the Wiki:
https://github.com/backdrop-contrib/feeds_comment_processor/wiki/Documentation.

## Issues

Bugs and Feature requests should be reported in the Issue Queue:
https://github.com/backdrop-contrib/feeds_comment_processor/issues.

## Current Maintainers
- [izmeez](https://github.com/izmeez).
- Seeking additional maintainers.

## Credits

Ported to Backdrop CMS by [izmeez](https://github.com/izmeez) with AI assistance.

Originally created for Drupal by [Andrew Levine](https://www.drupal.org/u/andrewlevine).
Further adapted for Drupal by [Dane Powell](https://www.drupal.org/user/339326).

Drupal version maintained by [Andrew Levine](https://www.drupal.org/u/andrewlevine) and [twistor](https://www.drupal.org/u/twistor).

## License

This project is GPL v2 software.
See the LICENSE.txt file in this directory for complete text.
