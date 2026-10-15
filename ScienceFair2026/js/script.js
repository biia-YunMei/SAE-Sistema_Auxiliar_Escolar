// ─── TEMA ───
;(function initTheme() {
  const html = document.documentElement
  const toggle = document.getElementById('themeToggle')
  const icon = document.getElementById('themeIcon')
  const label = document.querySelector('.theme-label')

  function applyTheme(theme) {
    if (theme === 'dark') {
      html.setAttribute('data-bs-theme', 'dark')
      icon.className = 'bi bi-sun-fill'
      if (label) label.textContent = 'Claro'
      localStorage.setItem('theme', 'dark')
    } else {
      html.setAttribute('data-bs-theme', 'light')
      icon.className = 'bi bi-moon-fill'
      if (label) label.textContent = 'Escuro'
      localStorage.setItem('theme', 'light')
    }
  }

  function getPreferredTheme() {
    const saved = localStorage.getItem('theme')
    if (saved) return saved
    if (
      window.matchMedia &&
      window.matchMedia('(prefers-color-scheme: dark)').matches
    ) {
      return 'dark'
    }
    return 'light'
  }

  applyTheme(getPreferredTheme())

  if (toggle) {
    toggle.addEventListener('click', function () {
      const current = html.getAttribute('data-bs-theme')
      applyTheme(current === 'dark' ? 'light' : 'dark')
    })
  }

  window
    .matchMedia('(prefers-color-scheme: dark)')
    .addEventListener('change', function (e) {
      if (!localStorage.getItem('theme')) {
        applyTheme(e.matches ? 'dark' : 'light')
      }
    })
})()

// ─── ESCAPE HTML PARA DADOS DA API ───
function saeEscapeHtml(value) {
  return String(value ?? '').replace(/[&<>"']/g, (char) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  })[char])
}

// ─── TOAST ───
function showToast(message, type = 'success') {
  const container = document.getElementById('toastContainer')
  if (!container) return
  const iconMap = {
    success: 'bi-check-circle-fill',
    danger: 'bi-exclamation-triangle-fill',
    warning: 'bi-exclamation-triangle-fill',
    info: 'bi-info-circle-fill',
  }
  const icon = iconMap[type] || iconMap.success
  const toastClass =
    type === 'success'
      ? 'toast-success'
      : type === 'danger'
        ? 'toast-danger'
        : type === 'warning'
          ? 'toast-warning'
          : 'toast-info'

  const toastHtml = `
    <div class="toast ${toastClass} align-items-center" role="alert" aria-live="assertive" aria-atomic="true" data-bs-delay="3500">
      <div class="d-flex">
        <div class="toast-body">
          <i class="bi ${icon}"></i>
          ${saeEscapeHtml(message)}
        </div>
        <button type="button" class="btn-close me-2 m-auto" data-bs-dismiss="toast" aria-label="Fechar"></button>
      </div>
    </div>
  `
  container.insertAdjacentHTML('beforeend', toastHtml)
  const toastEl = container.lastElementChild
  const bsToast = new bootstrap.Toast(toastEl, { autohide: true, delay: 3500 })
  bsToast.show()
  toastEl.addEventListener('hidden.bs.toast', function () {
    this.remove()
  })
}

// ─── FORMATAÇÃO ───
function formatDate(dateStr) {
  if (!dateStr) return '—'
  const d = new Date(dateStr + 'T00:00:00')
  return d.toLocaleDateString('pt-BR', {
    day: '2-digit',
    month: '2-digit',
    year: 'numeric',
  })
}

// ─── MÁSCARA WHATSAPP: 55 (xx) xxxxx-xxxx ───
// Aceita qualquer entrada (inclusive colagem), extrai apenas dígitos,
// garante o prefixo 55 (DDI Brasil) e formata.
function aplicarMascaraWhats(valor) {
  let digits = String(valor || '').replace(/\D/g, '')
  if (!digits) return ''
  // Se já digitou pelo menos 3 dígitos e não começou com 55, assume DDI 55
  if (digits.length > 2 && !digits.startsWith('55')) {
    digits = '55' + digits
  }
  digits = digits.slice(0, 13) // 55 + DDD(2) + 9 dígitos
  if (!digits.startsWith('55')) return digits

  const ddd = digits.slice(2, 4)
  const rest = digits.slice(4)

  if (digits.length === 2) return '55'
  if (digits.length <= 4) return `55 (${ddd}`
  if (digits.length <= 9) return `55 (${ddd}) ${rest}`
  return `55 (${ddd}) ${rest.slice(0, 5)}-${rest.slice(5)}`
}

// ─── LISTA DE ANOS/TURMAS ───
// Usada apenas para ORDENAR a exibição em formatarTurmas.
const TURMA_ANOS = [
  'Maternal I',
  'Maternal II',
  'Pré I',
  'Pré II',
  '1º EF',
  '2º EF',
  '3º EF',
  '4º',
  '5º',
  '6º',
  '7º',
  '8º',
  '9º',
  '1º EM',
  '2º EM',
  '3º EM',
]

// ─── UTILITÁRIO: separa "Maternal I A" em { ano: "Maternal I", letra: "A" } ───
function splitTurma(turma) {
  if (!turma) return { ano: '', letra: '' }
  const idx = turma.lastIndexOf(' ')
  if (idx === -1) return { ano: turma, letra: '' }
  return { ano: turma.slice(0, idx), letra: turma.slice(idx + 1) }
}

// ─── CAMPO DE TURMA (input de texto livre) ───
// Substitui o antigo select + checkboxes. O usuário digita a(s) turma(s)
// separadas por vírgula — ex.: "9º A, 9º B" ou "Maternal I A".
let SAE_TURMAS = null

function escTurma(v) {
  return String(v ?? '')
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
}

function gerarSelectorTurmas(containerId, turmasSelecionadas = []) {
  const container = document.getElementById(containerId)
  if (!container) return
  container.setAttribute('data-turmas-auto', '1')

  const marcadas = Array.isArray(turmasSelecionadas)
    ? turmasSelecionadas
    : turmasSelecionadas
      ? [turmasSelecionadas]
      : []
  container.dataset.marcadas = JSON.stringify(marcadas)

  if (SAE_TURMAS === null) {
    container.innerHTML = '<small class="text-muted">Carregando turmas…</small>'
    return
  }
  const opcoes = (container.dataset.todas === '1' ? [['Todas', 'Todas as turmas']] : [])
    .concat(SAE_TURMAS.map((n) => [n, n]))
  if (!SAE_TURMAS.length) {
    container.innerHTML = '<small class="text-warning">Nenhuma turma cadastrada ainda. A coordenação cadastra na aba Turmas.</small>'
    return
  }
  container.innerHTML = opcoes
    .map(
      ([valor, rotulo], i) => `
    <div class="form-check form-check-inline">
      <input class="form-check-input turma-check" type="checkbox" id="${containerId}-t${i}" value="${escTurma(valor)}"${marcadas.includes(valor) ? ' checked' : ''} />
      <label class="form-check-label" for="${containerId}-t${i}">${escTurma(rotulo)}</label>
    </div>`,
    )
    .join('')
}

function getTurmasSelecionadas(containerId) {
  const container = document.getElementById(containerId)
  if (!container) return []
  const marcadas = [...container.querySelectorAll('.turma-check:checked')].map((c) => c.value)
  if (marcadas.length || container.querySelector('.turma-check')) return marcadas
  const input = container.querySelector('.turma-input')
  if (input) return input.value.split(',').map((s) => s.trim()).filter(Boolean)
  try { return JSON.parse(container.dataset.marcadas || '[]') } catch (_) { return [] }
}

function saeDefinirTurmas(nomes) {
  SAE_TURMAS = (nomes || []).slice()
  document.querySelectorAll('[data-turmas-auto]').forEach((c) => {
    gerarSelectorTurmas(c.id, getTurmasSelecionadas(c.id))
  })
}

function saeDataServidor(valor) {
  if (!valor) return null
  const s = String(valor).trim()
  const semFuso = /^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}(:\d{2}(\.\d+)?)?$/.test(s)
  const d = new Date(semFuso ? s.replace(' ', 'T') + 'Z' : s)
  return isNaN(d.getTime()) ? null : d
}

function saeDiaLocal(valor) {
  const d = saeDataServidor(valor)
  if (!d) return String(valor || '').slice(0, 10)
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
}

// Formata a exibição das turmas (ex: ["9º A", "9º B"] -> "9º A e B")
function formatarTurmas(turmasArray) {
  if (!turmasArray || turmasArray.length === 0) return ''
  const grupos = {}
  turmasArray.forEach((t) => {
    const { ano, letra } = splitTurma(t)
    if (!grupos[ano]) grupos[ano] = []
    if (letra) grupos[ano].push(letra)
  })

  const anosOrdenados = Object.keys(grupos).sort((a, b) => {
    const ia = TURMA_ANOS.indexOf(a)
    const ib = TURMA_ANOS.indexOf(b)
    if (ia === -1 && ib === -1) return a.localeCompare(b)
    if (ia === -1) return 1
    if (ib === -1) return -1
    return ia - ib
  })

  const resultado = []
  for (const ano of anosOrdenados) {
    const letras = [...new Set(grupos[ano])].sort()
    if (letras.length === 0) {
      resultado.push(ano)
    } else if (
      letras.length === 2 &&
      letras.includes('A') &&
      letras.includes('B')
    ) {
      resultado.push(`${ano} A e B`)
    } else {
      resultado.push(`${ano} ${letras.join(' e ')}`)
    }
  }
  return resultado.join(', ')
}

// Conta turmas únicas por ANO (9º A e 9º B contam como uma só)
function contarTurmasUnicasPorAno(lista) {
  const anos = new Set()
  ;(lista || []).forEach((item) => {
    ;(item.turmas || []).forEach((t) => {
      const { ano } = splitTurma(t)
      if (ano) anos.add(ano)
    })
  })
  return anos.size
}

// ─── INICIALIZAÇÃO ───
function initTurmaSelector(containerId, turmas = []) {
  gerarSelectorTurmas(containerId, turmas)
}
