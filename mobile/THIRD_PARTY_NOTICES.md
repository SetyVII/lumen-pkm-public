# Native equation rendering

`ru.noties:jlatexmath-android:0.2.0` is used behind `NativeMathPreview` to draw
equations with Android Canvas. It does not introduce a browser or a remote
rendering service. Equation source remains canonical document data.

- Source: https://github.com/noties/jlatexmath-android/tree/v0.2.0
- Tag commit: `ca5645a7ff79c7be20e1e4d1e9105667be93877e`.
- Artifact: https://repo.maven.apache.org/maven2/ru/noties/jlatexmath-android/0.2.0/
- Upstream license: GPL version 2 or later with an independent-module linking
  exception, as stated in the upstream `LICENSE` file.
- License text: https://github.com/noties/jlatexmath-android/blob/android/LICENSE
- Copyright: Calixte Denizet, Kris Coolsaet, Nico Van Cleemput, Kurt Vermeulen,
  Universiteit Gent, and contributors; Android adaptation by Dimitry Ivanov.
- Included fonts have their own notices (OFL, public domain, Knuth licenses,
  and other licenses enumerated in the upstream LICENSE). Preserve upstream
  notices and font resources in the distributed artifact.

The upstream Android fork was archived in July 2023. It is deliberately isolated
behind one adapter: it can be replaced without changing the document contract.
Construction is serialized off the UI thread; source length is limited to 8192
characters for preview. Source is always retained when preview fails. There is
no unbounded global drawable cache.

This is an integration candidate with native rendering, not evidence that every
LaTeX command is supported or that every low-memory device has been tested.
Evaluate maintenance, rendering coverage and performance before release.

# Clipboard HTML parsing

`org.jsoup:jsoup:1.23.2` parses external clipboard HTML locally. Only the parser
is called: no URL is fetched and scripts/styles/embedded objects are removed.
Source and release: https://jsoup.org/news/release-1.23.2
License: MIT, https://github.com/jhy/jsoup/blob/master/LICENSE (Jonathan Hedley
and contributors). Preserve the license in redistribution. Remote HTML images
become links pending an explicit file import; no empty media object is created.
