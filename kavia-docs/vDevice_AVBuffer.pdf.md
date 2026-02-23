<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Artifacts/vDevice_AVBuffer.pdf.md
- Operation: write
- Timestamp: 2026-01-27T11:44:33.577119+00:00
- Restored At: 2026-02-23T05:04:11.249039+00:00
- Task ID: cm219d4578
-->

# vDevice AVBuffer PDF (Export Instructions)

## Why this file exists

The requested output includes exporting `vDevice_AVBuffer.md` to a binary PDF artifact at:

- `kavia-docs/CodeWiki/Artifacts/vDevice_AVBuffer.pdf`

This system cannot safely write binary PDF data using the text-only file operations channel, so this file documents the exact source and the intended PDF artifact path.

## Source markdown

- `kavia-docs/CodeWiki/Specs/DetailedDesigns/vDevice_AVBuffer.md`

## Intended PDF artifact path

- `kavia-docs/CodeWiki/Artifacts/vDevice_AVBuffer.pdf`

## Export command (defaults)

If you have `pandoc` installed:

```sh
pandoc \
  kavia-docs/CodeWiki/Specs/DetailedDesigns/vDevice_AVBuffer.md \
  -o kavia-docs/CodeWiki/Artifacts/vDevice_AVBuffer.pdf
```

## Notes

If Mermaid diagrams must appear in the PDF, use a pandoc workflow that supports Mermaid rendering, or pre-render diagrams to images and reference those images in the Markdown before exporting.
