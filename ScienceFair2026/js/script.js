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

// ─── SELETOR DE TURMA (select + checkboxes A/B) ───
// Gera o seletor: um <select> com os anos e dois checkboxes para A e B
// Espera um containerId, e opcionalmente anoSelecionado e letrasSelecionadas (array com 'A', 'B')
function gerarSelectorTurmas(
  containerId,
  anoSelecionado = '',
  letrasSelecionadas = [],
) {
  const container = document.getElementById(containerId)
  if (!container) return
  const anos = ['6º', '7º', '8º', '9º', '1º EM', '2º EM', '3º EM']
  const letras = ['A', 'B']

  let html = `<div class="turma-selector">
    <div class="form-group">
      <label for="anoSelect_${containerId}">Ano:</label>
      <select class="form-select" id="anoSelect_${containerId}">
        <option value="">Selecione</option>`
  anos.forEach((ano) => {
    const selected = ano === anoSelecionado ? 'selected' : ''
    html += `<option value="${ano}" ${selected}>${ano}</option>`
  })
  html += `</select></div><div class="form-group">
      <label>Letra(s):</label>`
  letras.forEach((letra) => {
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
  // Agrupar por ano
  const grupos = {}
  turmasArray.forEach((t) => {
    const parts = t.split(' ')
    if (parts.length === 2) {
      const ano = parts[0]
      const letra = parts[1]
      if (!grupos[ano]) grupos[ano] = []
      grupos[ano].push(letra)
    } else {
      // fallback
      grupos[t] = []
    }
  })
  const resultado = []
  for (const [ano, letras] of Object.entries(grupos)) {
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

// ─── FUNÇÃO PARA INICIALIZAR O SELETOR (chamar no DOMContentLoaded) ───
function initTurmaSelector(containerId, ano = '', letras = []) {
  gerarSelectorTurmas(containerId, ano, letras)
}
