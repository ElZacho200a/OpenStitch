OpenStitch Studio - Compagnon IA (optionnel)
==============================================

Ce dossier contient openstitch-mcp.exe, un serveur MCP qui permet a un agent IA local
(par exemple Claude Code) de lire et de modifier le projet OUVERT dans OpenStitch Studio.

  1. Dans OpenStitch Studio : menu Aide > Pont IA local (MCP). Desactive par defaut.
  2. Declarez dans votre client MCP : "command": "<ce dossier>\openstitch-mcp.exe",
     "args": ["--live"]
  3. Chaque modification demande votre approbation dans le panneau Compagnon IA et
     s'annule en une etape (Ctrl+Z).

Donnees : le pont est LOCAL (tube nomme accessible a votre compte seul, aucun port reseau).
Rien ne quitte votre machine sauf si vous activez le fournisseur Claude (Aide > Compagnon IA...) :
vos messages et les resultats d'outils sont alors envoyes a api.anthropic.com avec VOTRE cle API
(stockee dans le Gestionnaire d'identifiants Windows, retiree a la desinstallation).

Sans reseau, sans cle ou sans ce dossier, OpenStitch Studio fonctionne normalement.
Licence : Apache-2.0 (voir LICENSE). Aucun SDK ni binaire tiers n'est ajoute par le compagnon.
