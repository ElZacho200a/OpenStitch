#!/usr/bin/env bash

# Récupère les pages d'une base de données Notion

NOTION_API_KEY="${NOTION_API_KEY:?Set NOTION_API_KEY in the environment}"

# Corps de la requête search - recherche pages partagées du workspace OpenStitch
API_BODY=$(printf '{"filter":{"property":"object","text":{"rich_text":[{"type":"string","content":{"text":"OpenStitch","direction":"ltr"}}]},"in_trash":false},"page_size":10}')

curl -s \
  -H "Authorization: Bearer ${NOTION_API_KEY}" \
  -H "Notion-Version: 2022-06-28" \
  -H "Content-Type: application/json" \
  -d "$API_BODY" \
  "https://api.notion.com/v1/search" | cat
