/* Comunicação do front-end SAE com o servidor C. */
const saeDefaultApi = (window.location.protocol === 'http:' || window.location.protocol === 'https:')
  ? `${window.location.origin}/api`
  : 'http://127.0.0.1:8080/api'
const SAE_API = localStorage.getItem('saeApiUrl') || saeDefaultApi

/* Ao abrir arquivos HTML diretamente, alguns navegadores isolam o armazenamento
 * por página. Recebemos o token uma vez no redirecionamento e o persistimos
 * na página de destino antes de limpar a URL. */
;(function restoreSessionFromUrl() {
  const url = new URL(window.location.href)
  const token = url.searchParams.get('sae_token')
  if (!token) return
  localStorage.setItem('saeToken', token)
  url.searchParams.delete('sae_token')
  window.history.replaceState({}, '', url)
})()

async function saeRequest(path, options = {}) {
  const token = localStorage.getItem('saeToken')
  let response
  try {
    response = await fetch(`${SAE_API}${path}`, {
      ...options,
      headers: {
        'Content-Type': 'application/json',
        ...(token ? { Authorization: `Bearer ${token}` } : {}),
        ...(options.headers || {}),
      },
    })
  } catch (_) {
    throw new Error('Não foi possível conectar ao SAE. Inicie o servidor com “sae servidor” e tente novamente.')
  }
  const data = await response.json().catch(() => ({}))
  if (!response.ok) throw new Error(data.message || 'Não foi possível concluir a operação.')
  return data
}

async function saeLogin(phone, password) {
  const digits = String(phone || '').replace(/\D/g, '')
  const normalizedPhone = digits.length === 10 || digits.length === 11
    ? `+55${digits}`
    : `+${digits}`
  const credentials = { phone: normalizedPhone }
  if (password) credentials.password = password
  const data = await saeRequest('/auth', {
    method: 'POST', body: JSON.stringify(credentials),
  })
  if (!data.token || !data.type) throw new Error('O servidor retornou uma resposta de login inválida.')
  localStorage.setItem('saeToken', data.token)
  localStorage.setItem('saeUserType', data.type)
  return data
}

function saeRequireLogin() {
  if (!localStorage.getItem('saeToken')) {
    window.location.replace('index.html')
    return false
  }
  return true
}

function saeLogout() {
  localStorage.removeItem('saeToken')
  localStorage.removeItem('saeUserType')
  window.location.replace('index.html')
}
