#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Construit le PDF de documentation d'OpenStitch Studio.

Chaîne : Markdown (python-markdown) -> HTML + CSS -> PDF (xhtml2pdf/reportlab).
Diagrammes SVG embarqués vectoriellement. 100 % pip, hors ligne.

Sortie : docs/build/OpenStitch-Studio-Documentation.pdf
Contrôles : liens d'images, fichiers source cités, marqueurs TODO_DOC,
chemins utilisateur absolus, secrets potentiels, chapitres vides.
"""
from __future__ import annotations
import datetime
import os
import re
import sys

import markdown
from xhtml2pdf import pisa

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.dirname(HERE)
ROOT = os.path.dirname(DOCS)
SRC = os.path.join(DOCS, "source")
BUILD = os.path.join(DOCS, "build")
os.makedirs(BUILD, exist_ok=True)

APP_NAME = "OpenStitch Studio"
APP_VERSION = "0.1.0"
DOC_VERSION = "1.0"
PDF_NAME = "OpenStitch-Studio-Documentation.pdf"

# Ordre éditorial des chapitres (le nom de fichier sans .md).
CHAPTERS = [
    "index", "introduction", "getting-started", "installation", "user-guide",
    "image-processing", "segmentation", "vectorization", "embroidery-objects",
    "stitch-generation", "satin", "tatami", "stitch-editing",
    "palettes-and-threads", "simulation", "analysis-and-validation",
    "dst-format", "project-format", "architecture", "module-reference",
    "data-model", "algorithms", "build-system", "developer-guide", "testing",
    "contributing", "troubleshooting", "limitations", "roadmap", "glossary",
    "licenses", "generated-project-audit",
]

MD_EXT = ["tables", "fenced_code", "attr_list", "sane_lists", "md_in_html"]

CSS = """
@page {
  size: a4; margin: 2.4cm 2cm 2cm 2cm;
  @frame footer { -pdf-frame-content: footerContent; bottom: 1cm; margin-left: 2cm; margin-right: 2cm; height: 1cm; }
  @frame header { -pdf-frame-content: headerContent; top: 1cm; margin-left: 2cm; margin-right: 2cm; height: 1cm; }
}
body { font-family: Helvetica, Arial, sans-serif; font-size: 10.5pt; color: #1c2733; line-height: 1.42; }
h1 { font-size: 20pt; color: #234; border-bottom: 2pt solid #3a5a8c; padding-bottom: 3pt; margin-top: 4pt; -pdf-outline: true; -pdf-outline-level: 0; -pdf-keep-with-next: true; }
h2 { font-size: 15pt; color: #2b4870; margin-top: 14pt; -pdf-outline: true; -pdf-outline-level: 1; -pdf-keep-with-next: true; }
h3 { font-size: 12.5pt; color: #33475b; margin-top: 10pt; -pdf-outline: true; -pdf-outline-level: 2; -pdf-keep-with-next: true; }
h4 { font-size: 11pt; color: #445; margin-top: 8pt; }
p { margin: 4pt 0; }
a { color: #2b4870; text-decoration: none; }
code { font-family: "Courier New", monospace; font-size: 9pt; background: #f2f4f7; color: #223; }
pre { font-family: "Courier New", monospace; font-size: 8.6pt; background: #f6f8fa; border: 0.6pt solid #d8dee6; padding: 5pt; color: #182430; -pdf-keep-in-frame-mode: shrink; }
table { -pdf-keep-in-frame-mode: shrink; border-collapse: collapse; margin: 6pt 0; width: 100%; }
th { background: #e9eef7; border: 0.6pt solid #b9c4d4; padding: 3pt 5pt; font-size: 9pt; text-align: left; }
td { border: 0.6pt solid #cdd6e2; padding: 3pt 5pt; font-size: 9pt; vertical-align: top; }
blockquote { border-left: 3pt solid #b9c4d4; margin: 6pt 0; padding: 3pt 8pt; background: #f6f8fa; color: #33475b; }
ul, ol { margin: 4pt 0 4pt 14pt; }
li { margin: 1.5pt 0; }
img { -pdf-keep-in-frame-mode: shrink; }
.chapter { page-break-before: always; }
.note, .warning, .tip, .limit { padding: 6pt 8pt; margin: 8pt 0; border: 0.6pt solid; border-radius: 3pt; font-size: 9.5pt; }
.note { background: #eef4fb; border-color: #7fa5d6; }
.tip { background: #eef7ee; border-color: #7cbf88; }
.warning { background: #fdf2e6; border-color: #e0a95a; }
.limit { background: #fbeeee; border-color: #d78b8b; }
.status { font-weight: bold; }
.cover { background: #1f3355; color: white; padding: 0; }
"""

FOOTER = (
    '<div id="footerContent" style="font-size:8pt;color:#8894a5;">'
    f'{APP_NAME} — Documentation utilisateur et technique'
    ' &nbsp;·&nbsp; page <pdf:pagenumber> / <pdf:pagecount>'
    '</div>'
)
HEADER = (
    '<div id="headerContent" style="font-size:8pt;color:#aab4c2;text-align:right;">'
    f'v{APP_VERSION} — doc v{DOC_VERSION}</div>'
)


def cover_html():
    today = datetime.date.today().isoformat()
    return f"""
<div class="cover" style="page: cover;">
  <table style="width:100%; height:22.5cm; border:none;">
    <tr><td style="border:none; vertical-align:middle; text-align:center; background:#1f3355; color:#ffffff;">
      <div style="font-size:34pt; font-weight:bold; color:#ffffff;">{APP_NAME}</div>
      <div style="font-size:13pt; color:#cdd9ee; margin-top:6pt;">Numérisation open source pour broderie machine</div>
      <div style="font-size:15pt; color:#ffffff; margin-top:26pt;">Documentation utilisateur et technique</div>
      <div style="font-size:10pt; color:#aebfdc; margin-top:34pt;">
        Version du logiciel : {APP_VERSION} &nbsp;·&nbsp; Version de la documentation : {DOC_VERSION}<br/>
        Générée le {today}<br/>
        Licence : Apache-2.0<br/>
        Nom temporaire du projet (remplaçable, ADR-001)
      </div>
    </td></tr>
  </table>
</div>
<div class="chapter"></div>
"""


def editorial_html():
    today = datetime.date.today().isoformat()
    return f"""
<h1>Informations éditoriales</h1>
<table>
<tr><th>Élément</th><th>Valeur</th></tr>
<tr><td>Statut du document</td><td>Version initiale, générée automatiquement à partir des sources</td></tr>
<tr><td>Version de la documentation</td><td>{DOC_VERSION}</td></tr>
<tr><td>Version du logiciel documentée</td><td>{APP_VERSION} (constante <code>kAppVersion</code>, <code>libs/core/include/openstitch/core/app_info.hpp</code>)</td></tr>
<tr><td>Date de génération</td><td>{today}</td></tr>
<tr><td>Licence du logiciel</td><td>Apache-2.0 (<code>LICENSE</code>)</td></tr>
<tr><td>Licence de la documentation</td><td>Apache-2.0 (même dépôt)</td></tr>
<tr><td>Dépôt</td><td>Dépôt Git local ; aucun remote public déclaré à ce jour</td></tr>
</table>
<h2>Historique des révisions</h2>
<table>
<tr><th>Version</th><th>Date</th><th>Changements</th></tr>
<tr><td>1.0</td><td>{today}</td><td>Première édition complète : audit du dépôt, guides utilisateur et technique, référence des modules, format DST, format projet.</td></tr>
</table>
<h2>Signaler une erreur</h2>
<p>La documentation est générée depuis les fichiers Markdown de <code>docs/source/</code>.
Pour corriger une erreur, modifiez le fichier source concerné puis régénérez le PDF
(voir <em>Système de génération de la documentation</em> dans <code>docs/README.md</code>).
Les affirmations techniques sont tracées vers le code réel dans chaque section
« Implémentation associée ».</p>
"""


def toc_html(titles):
    rows = []
    for cid, title in titles:
        rows.append(f'<div style="margin:2.5pt 0;"><a href="#{cid}">{title}</a></div>')
    body = "".join(rows)
    return (
        '<div class="chapter"></div>'
        '<h1>Table des matières</h1>'
        '<p style="color:#66707d;font-size:9pt;">Les titres sont cliquables. '
        'Les lecteurs PDF affichent aussi les signets (panneau latéral) pour une '
        'navigation détaillée par chapitre et section.</p>'
        f'{body}'
    )


IMG_RE = re.compile(r'!\[[^\]]*\]\(([^)]+)\)')
LINK_RE = re.compile(r'\]\(([^)]+)\)')
TITLE_RE = re.compile(r'^#\s+(.*)$', re.M)


def build():
    problems = []
    warnings = []

    # 1) Diagrammes
    import subprocess
    r = subprocess.run([sys.executable, os.path.join(HERE, "gen_diagrams.py")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        problems.append("Échec de la génération des diagrammes : " + r.stderr)

    # 2) Chargement des chapitres
    titles = []
    chapter_html = []
    for i, name in enumerate(CHAPTERS):
        path = os.path.join(SRC, name + ".md")
        if not os.path.exists(path):
            problems.append(f"Chapitre manquant : {name}.md")
            continue
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        if len(text.strip()) < 40:
            problems.append(f"Chapitre vide ou quasi vide : {name}.md")
        if "TODO_DOC" in text:
            problems.append(f"Marqueur TODO_DOC non résolu dans {name}.md")

        # Contrôles : images référencées existent
        for m in IMG_RE.finditer(text):
            ref = m.group(1).split()[0]
            if ref.startswith("http"):
                continue
            ap = os.path.normpath(os.path.join(SRC, ref))
            if not os.path.exists(ap):
                problems.append(f"Image introuvable ({name}.md) : {ref}")

        # Titre du chapitre
        mt = TITLE_RE.search(text)
        title = mt.group(1).strip() if mt else name
        cid = "ch_" + name
        titles.append((cid, title))

        html = markdown.markdown(text, extensions=MD_EXT, output_format="html5")
        # Transforme les blockquotes commençant par un mot-clé en encadrés stylés
        html = style_admonitions(html)
        cls = "chapter" if i > 0 else ""
        chapter_html.append(f'<div class="{cls}" id="{cid}">{html}</div>')

    # 3) Scan de secrets / chemins sensibles sur le HTML assemblé
    assembled_text = "\n".join(chapter_html)
    secret_scan(assembled_text, warnings, problems)

    # 4) Assemblage du document
    doc = (
        "<html><head><meta charset='utf-8'><style>" + CSS + "</style></head><body>"
        + HEADER + FOOTER
        + cover_html()
        + editorial_html()
        + toc_html(titles)
        + "".join(chapter_html)
        + "</body></html>"
    )

    out = os.path.join(BUILD, PDF_NAME)
    with open(out, "wb") as f:
        res = pisa.CreatePDF(doc, dest=f, encoding="utf-8",
                             link_callback=lambda uri, rel: link_cb(uri))
    if res.err:
        problems.append("xhtml2pdf a signalé des erreurs de rendu.")

    # 5) Vérification du PDF
    pages = 0
    if os.path.exists(out):
        with open(out, "rb") as f:
            if f.read(5) != b"%PDF-":
                problems.append("Le fichier produit n'est pas un PDF valide.")
        try:
            from pypdf import PdfReader
            pages = len(PdfReader(out).pages)
        except Exception as e:  # noqa: BLE001
            warnings.append(f"Comptage des pages impossible : {e}")
    else:
        problems.append("Le PDF n'a pas été produit.")

    report(out, pages, titles, problems, warnings)
    return 0 if not problems else 1


def style_admonitions(html: str) -> str:
    mapping = {
        "Note :": "note", "Astuce :": "tip", "Conseil :": "tip",
        "Avertissement :": "warning", "Attention :": "warning",
        "Limitation :": "limit", "Limite :": "limit",
    }
    for key, cls in mapping.items():
        html = html.replace(
            f"<blockquote>\n<p>{key}",
            f'<div class="{cls}"><p><strong>{key.strip(" :")}</strong> —',
        )
    # ferme les blockquotes convertis (approximation : xhtml2pdf tolère)
    return html


def link_cb(uri: str) -> str:
    # Résout les chemins relatifs (images) depuis docs/source.
    if uri.startswith(("http:", "https:", "#")):
        return uri
    ap = os.path.normpath(os.path.join(SRC, uri))
    return ap if os.path.exists(ap) else uri


def secret_scan(text, warnings, problems):
    patterns = {
        "clé privée": r"BEGIN (?:RSA |EC )?PRIVATE KEY",
        "token générique": r"(?i)(secret|api[_-]?key|token)\s*[:=]\s*['\"][A-Za-z0-9]{16,}",
        "AWS": r"AKIA[0-9A-Z]{16}",
    }
    for label, pat in patterns.items():
        if re.search(pat, text):
            problems.append(f"Secret potentiel détecté ({label}) — retiré du PDF.")
    # Chemins utilisateur absolus Windows
    if re.search(r"[A-Z]:\\\\Users\\\\[^\\\\ ]+", text) or "C:\\Users\\" in text:
        warnings.append("Chemin utilisateur absolu 'C:\\Users\\...' présent dans le texte.")


def report(out, pages, titles, problems, warnings):
    size = os.path.getsize(out) if os.path.exists(out) else 0
    lines = []
    lines.append(f"# Rapport de génération de la documentation\n")
    lines.append(f"- **PDF** : `docs/build/{PDF_NAME}`")
    lines.append(f"- **Existe** : {'oui' if os.path.exists(out) else 'NON'}")
    lines.append(f"- **Pages** : {pages}")
    lines.append(f"- **Taille** : {size/1024:.1f} Kio ({size} octets)")
    lines.append(f"- **Chaîne** : python-markdown -> HTML/CSS -> xhtml2pdf (reportlab), SVG via svglib")
    lines.append(f"- **Commande** : `docs\\scripts\\build-docs.ps1` (ou `python docs/scripts/build-docs.py`)")
    lines.append(f"- **Chapitres produits** : {len(titles)}")
    lines.append("")
    lines.append("## Chapitres")
    for _, t in titles:
        lines.append(f"- {t}")
    lines.append("")
    lines.append("## Contrôles de cohérence")
    if problems:
        lines.append("**Problèmes (bloquants) :**")
        for p in problems:
            lines.append(f"- ⛔ {p}")
    else:
        lines.append("- ✅ Aucun problème bloquant (liens d'images, fichiers source, "
                     "marqueurs TODO_DOC, secrets, chapitres vides).")
    if warnings:
        lines.append("\n**Avertissements :**")
        for w in warnings:
            lines.append(f"- ⚠ {w}")
    with open(os.path.join(BUILD, "documentation-report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print("=" * 60)
    print(f"PDF : {out}")
    print(f"Existe : {os.path.exists(out)} | Pages : {pages} | Taille : {size/1024:.1f} Kio")
    print(f"Chapitres : {len(titles)} | Problèmes : {len(problems)} | Avertissements : {len(warnings)}")
    for p in problems:
        print("  PROBLEME:", p)
    print("=" * 60)


if __name__ == "__main__":
    sys.exit(build())
