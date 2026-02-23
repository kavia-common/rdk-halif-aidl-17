<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/DetailedDesigns/assets/avbuffer-worked-example-annotated.svg
- Operation: write
- Timestamp: 2026-01-25T12:25:38.247574+00:00
- Restored At: 2026-02-23T05:04:11.244049+00:00
- Task ID: cm219d4578
-->

<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <defs>
    <marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M 0 0 L 10 5 L 0 10 z" fill="#d73a49"/>
    </marker>
    <style>
      .label { font-family: Arial, Helvetica, sans-serif; font-size: 10px; fill: #111; }
      .callout { fill: #ffffff; stroke: #111111; stroke-width: 1; opacity: 0.92; }
      .hiA { fill: none; stroke: #d73a49; stroke-width: 2; }
      .hiB { fill: none; stroke: #0366d6; stroke-width: 2; }
      .arrowA { stroke: #d73a49; stroke-width: 2; fill: none; marker-end: url(#arrow); }
      .arrowB { stroke: #0366d6; stroke-width: 2; fill: none; marker-end: url(#arrow); }
    </style>
  </defs>

  <!-- Base image (provided sample picture) -->
  <image href="../../../../attachments/20260125_121007_image.png" x="0" y="0" width="256" height="256" preserveAspectRatio="none"/>

  <!-- Highlight the first and last rows (approximate bands) -->
  <rect class="hiA" x="48" y="10" width="200" height="24"/>
  <rect class="hiB" x="48" y="210" width="200" height="24"/>

  <!-- Callout: top row meaning -->
  <rect class="callout" x="6" y="6" width="105" height="34"/>
  <text class="label" x="12" y="18">A: unsorted order</text>
  <text class="label" x="12" y="30">after insert/push</text>
  <path class="arrowA" d="M 110 25 L 48 22"/>

  <!-- Callout: bottom row meaning -->
  <rect class="callout" x="6" y="188" width="118" height="52"/>
  <text class="label" x="12" y="200">B: sorted by offset</text>
  <text class="label" x="12" y="212">used for gap scan</text>
  <text class="label" x="12" y="224">(FindFreeSpace)</text>
  <path class="arrowB" d="M 124 214 L 48 222"/>

  <!-- Callout: handle routing reminder -->
  <rect class="callout" x="132" y="188" width="118" height="62"/>
  <text class="label" x="138" y="200">Handle bits:</text>
  <text class="label" x="138" y="212">[ poolId:8 ][ seq:56 ]</text>
  <text class="label" x="138" y="224">free() routes by</text>
  <text class="label" x="138" y="236">poolId (top byte)</text>
</svg>
