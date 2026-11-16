# Récupère les pages d'une base de données Notion

$NOTION_API_KEY = $env:NOTION_API_KEY
if ([string]::IsNullOrWhiteSpace($NOTION_API_KEY)) {
    throw "Set NOTION_API_KEY in the environment before running this script."
}

$headers = @{
    "Authorization" = "Bearer $NOTION_API_KEY"
    "Notion-Version" = "2022-06-28"
    "Content-Type" = "application/json"
}

# Endpoint /v1/search - GET avec paramètre dans l'URL, pas de body
Write-Host "=== Recherche Notion (workspace) ===" -ForegroundColor Cyan

# Pour search API, on utilise POST avec body JSON contenant page_size et/ou text_query
$response = Invoke-RestMethod `
    -Uri "https://api.notion.com/v1/search?page_size=50" `
    -Headers $headers | ConvertTo-Json -Depth 10
Write-Host $response -ForegroundColor Green
