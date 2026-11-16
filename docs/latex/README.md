# FlowSim slides and report

| File | What it is |
| --- | --- |
| `slides.tex` | 16:9 Beamer deck, speaker notes are inside each frame (`\note{...}`) |
| `speaker_notes.tex` | Notes-only build of the deck: one page per slide with a thumbnail and the notes |
| `report.tex` | Detailed project report |
| `common.tex` | Shared colours, diagrams and charts |
| `data/` | CSV results from the real engine, used by the charts and tables |
| `figures/` | Screenshots of the app |

Build (needs `latexmk` and a TeX Live with Beamer, PGFPlots and TikZ):

```bash
cd docs/latex
latexmk -pdf slides.tex speaker_notes.tex report.tex
```

On Overleaf, upload this folder and compile `slides.tex`, `speaker_notes.tex` or `report.tex` with pdfLaTeX.

To regenerate `data/` after changing the engine or the sample scenario, build the engine and run:

```bash
python3 scripts/export_report_data.py
```
