<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Artifacts/avbuffer-manager-design.pdf.md
- Operation: write
- Timestamp: 2026-01-27T11:40:16.864558+00:00
- Restored At: 2026-02-23T05:04:11.248926+00:00
- Task ID: cm219d4578
-->

# AVBuffer Manager Design PDF (Export Instructions)

## Why this file exists

The requested output includes exporting `avbuffer-manager-design.md` to PDF. This system cannot safely write binary PDF data using the text-only file operations channel, so this file documents the exact source and the intended PDF artifact path.

## Source markdown

- `kavia-docs/CodeWiki/Specs/DetailedDesigns/avbuffer-manager-design.md`

## Intended PDF artifact path

- `kavia-docs/CodeWiki/Artifacts/avbuffer-manager-design.pdf`

## Export command (example)

If you have `pandoc` installed:

```sh
pandoc \
  kavia-docs/CodeWiki/Specs/DetailedDesigns/avbuffer-manager-design.md \
  -o kavia-docs/CodeWiki/Artifacts/avbuffer-manager-design.pdf
```

If you need Mermaid rendering in the PDF, use a pandoc workflow that supports Mermaid (or pre-render diagrams to images and reference them).
