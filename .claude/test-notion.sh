#!/usr/bin/env bash
# Test de connexion à l'API Notion

NOTION_API_KEY="${NOTION_API_KEY:?Set NOTION_API_KEY in the environment}"

echo "=== Test de connexion API Notion ==="
echo "API Key (tronquée): ${NOTION_API_KEY:0:10}..."
echo ""

# Recherche simple - body JSON est requis avec filter
BODY='{"filter":{},"page_size":5}'

echo "Requête: search dans le workspace Notion"
curl -s \
  -H "Authorization: Bearer $NOTION_API_KEY" \
  -H "Notion-Version: 2022-06-28" \
  -H "Content-Type: application/json" \
  -d "$BODY" \
  "https://api.notion.com/v1/search?page_size=5"

echo ""
echo ""
