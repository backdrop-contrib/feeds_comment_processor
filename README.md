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
4. Go to Comment processor mapping and configure source and targets.
5. Configure other settings as required.

## Future plans

Plans include adding integration for the [Comment Notify](https://backdropcms.org/project/comment_notify) module.

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
