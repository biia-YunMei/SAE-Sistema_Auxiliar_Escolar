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
          ${message}
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

// ─── MÁSCARA WHATSAPP ───
function aplicarMascaraWhats(valor) {
  let numeros = valor.replace(/\D/g, '')
  if (numeros.length > 11) numeros = numeros.slice(0, 11)
  let resultado = ''
  if (numeros.length > 0) {
    resultado += '(' + numeros.slice(0, 2)
  }
  if (numeros.length >= 3) {
    resultado += ') '
    if (numeros.length <= 7) {
      resultado += numeros.slice(2)
    } else {
      resultado += numeros.slice(2, 7) + '-' + numeros.slice(7)
    }
  }
  return resultado
}

// ─── LISTA DE ANOS/TURMAS ───
// A ordem aqui é a ordem exibida no <select>.
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
const TURMA_LETRAS = ['A', 'B']

// ─── UTILITÁRIO: separa "Maternal I A" em { ano: "Maternal I", letra: "A" } ───
// Regra: a letra é sempre o último caractere; o resto é o ano.
// Isso evita o bug de split(' ')[0] que devolveria "Maternal" em vez de "Maternal I".
function splitTurma(turma) {
  if (!turma) return { ano: '', letra: '' }
  const idx = turma.lastIndexOf(' ')
  if (idx === -1) return { ano: turma, letra: '' }
  return { ano: turma.slice(0, idx), letra: turma.slice(idx + 1) }
}

// ─── SELETOR DE TURMA (select de anos + checkboxes A/B) ───
function gerarSelectorTurmas(
  containerId,
  anoSelecionado = '',
  letrasSelecionadas = [],
) {
  const container = document.getElementById(containerId)
  if (!container) return

  let html = `<div class="turma-selector">
    <div class="form-group">
      <label for="anoSelect_${containerId}">Ano/Turma:</label>
      <select class="form-select" id="anoSelect_${containerId}">
        <option value="">Selecione</option>`
  TURMA_ANOS.forEach((ano) => {
    const selected = ano === anoSelecionado ? 'selected' : ''
    html += `<option value="${ano}" ${selected}>${ano}</option>`
  })
  html += `</select></div><div class="form-group">
      <label>Letra(s):</label>`
  TURMA_LETRAS.forEach((letra) => {
    const checked = letrasSelecionadas.includes(letra) ? 'checked' : ''
    html += `<div class="form-check form-check-inline">
      <input class="form-check-input" type="checkbox" id="letra_${containerId}_${letra}" value="${letra}" ${checked}>
      <label class="form-check-label" for="letra_${containerId}_${letra}">${letra}</label>
    </div>`
  })
  html += `</div></div>`
  container.innerHTML = html
}

// Obtém as turmas selecionadas a partir do seletor
function getTurmasSelecionadas(containerId) {
  const container = document.getElementById(containerId)
  if (!container) return []
  const select = container.querySelector('select')
  const ano = select ? select.value : ''
  if (!ano) return []
  const checkboxes = container.querySelectorAll(
    'input[type="checkbox"]:checked',
  )
  const letras = Array.from(checkboxes).map((el) => el.value)
  if (letras.length === 0) return []
  return letras.map((letra) => `${ano} ${letra}`)
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

  // Ordena pelos anos na ordem canônica; anos desconhecidos vão pro fim
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
function initTurmaSelector(containerId, ano = '', letras = []) {
  gerarSelectorTurmas(containerId, ano, letras)
}
