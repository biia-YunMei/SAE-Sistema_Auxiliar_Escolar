/* Comunicação do front-end SAE com o servidor C. */
;(function () {
  'use strict'

  function safeStorage(name) {
    const fallback = new Map()
    function nativeStorage() {
      try { return window[name] } catch (_) { return null }
    }
    return {
      getItem(key) {
        try {
          const store = nativeStorage()
          return store ? store.getItem(key) : (fallback.get(key) ?? null)
        } catch (_) { return fallback.get(key) ?? null }
      },
      setItem(key, value) {
        const text = String(value)
        fallback.set(key, text)
        try {
          const store = nativeStorage()
          if (store) store.setItem(key, text)
        } catch (_) {}
      },
      removeItem(key) {
        fallback.delete(key)
        try {
          const store = nativeStorage()
          if (store) store.removeItem(key)
        } catch (_) {}
      },
    }
  }

  const storage = safeStorage('sessionStorage')
  const local = safeStorage('localStorage')
  const localHosts = new Set(['localhost', '127.0.0.1', '::1'])
  const sameOriginApi = (window.location.protocol === 'http:' || window.location.protocol === 'https:')
    ? `${window.location.origin}/api`
    : 'http://127.0.0.1:8080/api'
  const hostedApi = window.location.hostname === 'escola.cidades.io'
    ? 'https://api.cidades.io/api/escola/v1/sae'
    : sameOriginApi

  function safeApiBase(value) {
    if (!value) return hostedApi
    try {
      const url = new URL(value)
      const path = url.pathname.replace(/\/+$/, '')
      const isLocalPage = localHosts.has(window.location.hostname)
      const isCurrentOrigin = url.origin === window.location.origin &&
        (url.protocol === 'https:' || isLocalPage)
      const isLocalApi = localHosts.has(url.hostname) &&
        (url.protocol === 'http:' || url.protocol === 'https:')
      const isHostedApi = url.origin === new URL(hostedApi).origin &&
        path === new URL(hostedApi).pathname.replace(/\/+$/, '')
      if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password ||
          url.search || url.hash || (!isCurrentOrigin && !isLocalApi && !isHostedApi)) return hostedApi
      if (path !== '/api' && !isHostedApi) return hostedApi
      return url.href.replace(/\/+$/, '')
    } catch (_) {
      return hostedApi
    }
  }

  const SAE_API = safeApiBase(local.getItem('saeApiUrl'))
  window.SAE_API = SAE_API
  window.saeBackendEnabled = window.location.protocol !== 'file:'

  /* Nunca transporta token na URL: URL pode aparecer no histórico e em logs. */
  const currentUrl = new URL(window.location.href)
  if (currentUrl.searchParams.has('sae_token')) {
    currentUrl.searchParams.delete('sae_token')
    window.history.replaceState({}, '', currentUrl)
  }

  function clearSession() {
    storage.removeItem('saeToken')
    storage.removeItem('saeUserType')
    local.removeItem('saeToken')
    local.removeItem('saeUserType')
  }

  async function saeRequest(path, options = {}) {
    if (window.location.protocol === 'file:') {
      throw new Error('Abra o SAE pelo servidor em http://127.0.0.1:8080, não pelo arquivo HTML.')
    }
    if (window.location.protocol !== 'https:' &&
        !localHosts.has(window.location.hostname)) {
      throw new Error('Acesse o SAE por HTTPS antes de enviar credenciais ou dados.')
    }
    if (typeof path !== 'string' || !path.startsWith('/') || path.startsWith('//')) {
      throw new Error('Endereço de API inválido.')
    }
    const token = storage.getItem('saeToken')
    let response
    try {
      response = await fetch(`${SAE_API}${path}`, {
        ...options,
        headers: {
          ...(options.body !== undefined ? { 'Content-Type': 'application/json' } : {}),
          ...(token ? { Authorization: `Bearer ${token}` } : {}),
          ...(options.headers || {}),
        },
        cache: 'no-store',
      })
    } catch (_) {
      throw new Error('Não foi possível conectar ao SAE. Inicie o servidor com “sae servidor” e tente novamente.')
    }
    const data = await response.json().catch(() => ({}))
    if (!response.ok) {
      if (response.status === 401 && path !== '/auth') {
        clearSession()
        if (!window.location.pathname.endsWith('/index.html') && window.location.pathname !== '/') {
          window.location.replace('index.html')
        }
      }
      throw new Error(data.message || 'Não foi possível concluir a operação.')
    }
    return data
  }

  async function saeRequestAll(path, pageSize = 20) {
    const separator = path.includes('?') ? '&' : '?'
    const items = []
    let offset = 0
    for (let page = 0; page < 500; page += 1) {
      const result = await saeRequest(`${path}${separator}limit=${pageSize}&offset=${offset}`)
      const rows = Array.isArray(result.items) ? result.items : []
      items.push(...rows)
      if (!result.has_more || rows.length === 0) return { ...result, items }
      offset += rows.length
    }
    throw new Error('A lista excede o limite seguro de páginas. Refine a consulta.')
  }

  async function saeLogin(phone, password) {
    const digits = String(phone || '').replace(/\D/g, '')
    if (digits.length < 8 || digits.length > 15) throw new Error('Informe um número de WhatsApp válido.')
    const normalizedPhone = digits.length === 10 || digits.length === 11
      ? `+55${digits}`
      : `+${digits}`
    const credentials = { phone: normalizedPhone }
    if (password) credentials.password = password
    const data = await saeRequest('/auth', {
      method: 'POST', body: JSON.stringify(credentials),
    })
    if (!data.token || !['COORD', 'PROF'].includes(data.type)) {
      throw new Error('Este usuário não tem acesso ao painel do SAE.')
    }
    storage.setItem('saeToken', data.token)
    storage.setItem('saeUserType', data.type)
    return data
  }

  function saeRequireLogin(expectedType) {
    const token = storage.getItem('saeToken')
    const type = storage.getItem('saeUserType')
    if (!token || (expectedType && type !== expectedType)) {
      clearSession()
      window.location.replace('index.html')
      return false
    }
    return true
  }

  const pageName = window.location.pathname.split('/').pop()
  const requiredPageType = pageName === 'coordenacao.html' ? 'COORD'
    : pageName === 'professores.html' ? 'PROF' : ''
  if (requiredPageType && !saeRequireLogin(requiredPageType)) {
    document.body.hidden = true
  }

  async function saeLogout() {
    try {
      if (storage.getItem('saeToken')) {
        await saeRequest('/auth/logout', { method: 'POST' })
      }
    } finally {
      clearSession()
      window.location.replace('index.html')
    }
  }

  /* Publica a API antes de qualquer lógica de página que possa redirecionar. */
  window.saeRequest = saeRequest
  window.saeRequestAll = saeRequestAll
  window.saeLogin = saeLogin
  window.saeRequireLogin = saeRequireLogin
  window.saeLogout = saeLogout
  document.addEventListener('click', (event) => {
    if (event.target.closest('[data-sae-logout]')) void saeLogout()
  })
})()
