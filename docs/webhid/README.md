# WebHID editor

`index.html` is the current browser UI for the ErgoType WebHID configuration interface.

Use the hosted page in a Chromium-based browser:

https://nazya.github.io/ErgoType/webhid/

The editor is also self-contained and can be kept locally. Open the local copy in a Chromium-based browser:

```sh
xdg-open docs/webhid/index.html
```

or open `docs/webhid/index.html` from the file manager. The GitHub file view shows the HTML source; it does not run the page.

## Files

- `ergotype-kle.json`: current base Keyboard Layout Editor layout used for the keyboard preview work.

## Current limitations

- The keyboard preview is hardcoded for the current ErgoType monosplit hardware.
- There is no hardware/layout selector yet.
- New hardware revisions will need their own layout metadata and key matrix mapping.

## Follow-up

Open a GitHub issue for hardware layout selection:

- add a hardware selector to the WebHID page;
- keep one KLE/layout mapping per supported hardware revision;
- select the correct layout from firmware/device metadata when that metadata exists;
- keep the current monosplit layout as the default until more hardware is defined.

Issue draft: [`hardware-selector-issue.md`](hardware-selector-issue.md).
