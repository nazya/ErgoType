# WebHID: add hardware layout selector

## Problem

The WebHID editor currently has one hardcoded keyboard preview/layout mapping for the current ErgoType monosplit hardware. This is enough for the current board, but it will not scale once firmware supports more hardware revisions.

## Goal

Add a hardware/layout selector to the WebHID page and keep one layout definition per supported hardware revision.

## Proposed scope

- Keep the current monosplit layout as the default.
- Move layout metadata into named hardware definitions.
- Add a selector in the WebHID UI for available hardware layouts.
- Eventually select the layout from firmware/device metadata when that metadata is exposed.
- Keep the KLE source for each hardware layout in docs/source control.

## Current limitation

There is no selector yet. The preview is hardcoded to the current ErgoType monosplit layout.
