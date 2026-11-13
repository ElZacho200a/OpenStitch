#!/usr/bin/env node

/**
 * Serveur MCP pour Notion - Expose les skills Notion activés
 */

import { NotionClient } from 'notion-sdk'
import { z } from 'zod'

const notion = new NotionClient({
  apiKey: process.env.NOTION_API_KEY || ''
})

// Recherche du projet OpenStitch dans Notion
async function listEnabledSkills() {
  try {
    console.log('Connexion à Notion...')
    
    // Essayer de rechercher la page "OpenStitch Project HQ"
    const searchResult = await notion.search({
      query: 'OpenStitch enabled skills',
      page_size: 5
    })
    
    console.log('Résultats de recherche:', JSON.stringify(searchResult, null, 2))
    
    if (searchResult.results && searchResult.results.length > 0) {
      const results = searchResult.results
      for (const item of results) {
        if (item.properties) {
          console.log('Page trouvée:', item.id, '-', item.properties.title?.[0]?.text || 'sans titre')
        }
      }
    }
    
    return {
      success: true,
      data: searchResult.results || []
    }
  } catch (error) {
    console.error('Erreur Notion:', error.message)
    return {
      success: false,
      error: error.message
    }
  }
}

// Lecture d'une page spécifique par ID
async function readPage(pageId) {
  try {
    const page = await notion.pages.retrieve({ page_id: pageId })
    console.log('Page lue:', JSON.stringify(page, null, 2))
    return page
  } catch (error) {
    console.error('Erreur lecture page:', error.message)
    return null
  }
}

export { listEnabledSkills, readPage }
