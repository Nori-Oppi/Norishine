// Cover search in LizardByte's GameDB (IGDB covers). The chosen cover is downloaded by the
// host (POST /api/covers/upload, images.igdb.com only).
const DB = 'https://raw.githubusercontent.com/LizardByte/GameDB/gh-pages'

function bucket(name) {
  const b = name.substring(0, Math.min(name.length, 2)).toLowerCase().replaceAll(/[^a-z\d]/g, '')
  return b || '@'
}

export async function searchCovers(name) {
  name = String(name || '').trim()
  if (!name) {
    return []
  }
  const search = name.replaceAll(/\s+/g, '.').toLowerCase()
  const r = await fetch(`${DB}/buckets/${bucket(name)}.json`)
  if (!r.ok) {
    throw new Error(`HTTP ${r.status}`)
  }
  const maps = await r.json()
  const ids = Object.keys(maps).filter((id) => maps[id].name.replaceAll(/\s+/g, '.').toLowerCase().startsWith(search)).slice(0, 40)
  const games = await Promise.all(ids.map((id) => fetch(`${DB}/games/${id}.json`).then((g) => g.json()).catch(() => null)))
  return games
    .filter((g) => g && g.cover && g.cover.url)
    .map((g) => {
      const thumb = g.cover.url
      const slug = thumb.substring(thumb.lastIndexOf('/') + 1, thumb.lastIndexOf('.'))
      return {
        name: g.name,
        key: `igdb_${g.id}`,
        url: `https://images.igdb.com/igdb/image/upload/t_cover_big/${slug}.jpg`,
        saveUrl: `https://images.igdb.com/igdb/image/upload/t_cover_big_2x/${slug}.png`,
      }
    })
}
