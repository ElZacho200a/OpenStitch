#!/usr/bin/env bash

# Script CLI pour Notion - Utilise l'API REST directement

set -euo pipefail

NOTION_API_KEY="${NOTION_API_KEY:-}"

if [[ -z "$NOTION_API_KEY" ]]; then
  echo "Erreur: NOTION_API_KEY non définie" >&2
  exit 1
fi

# Fonction pour appeler l'API Notion
call_notion() {
  local url="$1"
  shift
  
  curl \
    -s \
    -H "Authorization: Bearer $NOTION_API_KEY" \
    -H "Content-Type: application/json" \
    "${url}" \
    "$@"
}

# Lecture d'une page par ID
read_page() {
  local page_id="$1"
  
  call_notion "/v1/pages/${page_id}"
}

# Recherche dans Notion (search v1.0)
search() {
  local query="${1:-}"
  
  if [[ -z "$query" ]]; then
    query="OpenStitch"
  fi
  
  call_notion "/v1/search" \
    --data "application/json" \
    "{\"filter\": {}, \"page_size\": 50, \"sort\": {\"direction\": \"descending\", \"timestamp\": \"last_edited_time\"}, \"text\": {\"anchor\": \"object\", \"match_type\": \"substring\", \"query\": \"${query}\"}}"
}

# Lecture d'une propriété spécifique d'une page
read_page_property() {
  local page_id="$1"
  local property_name="${2:-title}"
  
  call_notion "/v1/pages/${page_id}/properties/${property_name}"
}

# Création d'une page (nécessite parentId)
create_page() {
  local parent_id="$1"
  local parent_type="${2:-database}"
  local properties_json="${3:-{\"title\": [{\"text\": {\"content\": \"New Page\"}}]}}"
  
  call_notion "/v1/pages" \
    --data "application/json" \
    "{\"parent\": {\"type\": \"${parent_type}\", \"id\": \"${parent_id}\"}, \"properties\": ${properties_json}}"
}

# Mise à jour d'une page
update_page() {
  local page_id="$1"
  local properties_json="${2:-}"
  
  call_notion "/v1/pages/${page_id}" \
    -X PATCH \
    --data "application/json" \
    "{\"parent\": {\"type\": \"database\", \"id\": \"$page_id\"}, \"properties\": ${properties_json}}"
}

# Listez toutes les pages d'une base de données
list_database_pages() {
  local database_id="$1"
  
  call_notion "/v1/databases/${database_id}" \
    --data "application/json" \
    "{\"filter\": {\"property\": \"Status\", \"checkbox\": {\"equals\": true}}}"
}

# Affiche l'aide
help() {
  echo "Usage: $0 <command> [arguments]"
  echo ""
  echo "Commandes:"
  echo "  read <page_id>                          - Lire une page par ID"
  echo "  search <query>                          - Rechercher dans Notion (ex: 'OpenStitch')"
  echo "  read-property <page_id> <property>     - Lire une propriété d'une page"
  echo "  create <parent_id> [type] [properties] - Créer une page"
  echo "  update <page_id> [properties]          - Mettre à jour une page"
  echo "  list-database <database_id>            - Lister les pages d'une base de données"
}

# Affiche la version des outils Notion MCP disponibles
available_tools() {
  echo "=== Outils Notion MCP disponibles ==="
  echo ""
  echo "1. notion-read-page - Lire une page Notion par ID"
  echo "2. notion-search - Rechercher des pages dans le workspace"
  echo "3. notion-read-property - Lire une propriété spécifique d'une page"
  echo "4. notion-create-page - Créer une nouvelle page/database"
  echo "5. notion-update-page - Mettre à jour une page existente"
  echo "6. notion-list-database - Lister les pages d'une database"
  echo ""
}

# Point d'entrée principal si script exécuté directement
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  case "${1:-help}" in
    read)
      if [[ -z "$2" ]]; then
        echo "Erreur: page_id requis" >&2
        exit 1
      fi
      read_page "$2"
      ;;
    search)
      if [[ -z "$2" ]]; then
        echo "Erreur: query requis" >&2
        exit 1
      fi
      search "$2"
      ;;
    read-property)
      if [[ -z "$2" || -z "$3" ]]; then
        echo "Erreur: page_id et property requis" >&2
        exit 1
      fi
      read_page_property "$2" "$3"
      ;;
    create)
      create_page "${2:-}" "${3:-database}" "${4:-}"
      ;;
    update)
      if [[ -z "$2" ]]; then
        echo "Erreur: page_id requis" >&2
        exit 1
      fi
      update_page "$2" "${3:-}"
      ;;
    list-database)
      if [[ -z "$2" ]]; then
        echo "Erreur: database_id requis" >&2
        exit 1
      fi
      list_database_pages "$2"
      ;;
    available|help|-h|--help|"")
      available_tools
      ;;
    *)
      echo "Commande inconnue: $1" >&2
      help
      exit 1
      ;;
  esac
fi
