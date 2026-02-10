// common.js — Shared benchmark rendering logic.
// Dynamically discovers categories and benchmarks from data.js naming hierarchy.
// Benchmark names follow: OptimizerName/Category/Detail.../iterations:N

(function () {
  'use strict';

  var COLORS = ['#4285f4', '#ea4335', '#34a853', '#fbbc04', '#ff6d01',
    '#46bdc6', '#9c27b0', '#795548', '#607d8b', '#e91e63'];

  // ── Unit formatting ──

  function fmtTime(ns) {
    if (ns >= 1e9) return (ns / 1e9).toFixed(2) + ' s';
    if (ns >= 1e6) return (ns / 1e6).toFixed(2) + ' ms';
    if (ns >= 1e3) return (ns / 1e3).toFixed(1) + ' \u00b5s';
    return Math.round(ns) + ' ns';
  }

  function bestUnit(values) {
    if (!values.length) return { d: 1e6, l: 'ms' };
    var mx = Math.max.apply(null, values);
    if (mx >= 1e9) return { d: 1e9, l: 's' };
    if (mx >= 1e6) return { d: 1e6, l: 'ms' };
    if (mx >= 1e3) return { d: 1e3, l: '\u00b5s' };
    return { d: 1, l: 'ns' };
  }

  // ── Data helpers ──

  function getData() {
    if (!window.BENCHMARK_DATA) return [];
    var e = window.BENCHMARK_DATA.entries;
    var key = Object.keys(e)[0];
    return e[key] || [];
  }

  function parseName(fullName) {
    var parts = fullName.split('/').filter(function (s) { return s.indexOf('iterations:') !== 0; });
    return {
      category: parts[1] || 'Default',
      detail: parts.slice(2).join(' / ') || parts[0]
    };
  }

  function discoverCategories(data) {
    if (!data.length) return {};
    var latest = data[data.length - 1];
    var cats = {};
    latest.benches.forEach(function (b) {
      var cat = parseName(b.name).category;
      if (!cats[cat]) cats[cat] = [];
      cats[cat].push(b.name);
    });
    return cats;
  }

  function timeSeries(data, benchName) {
    var series = [];
    data.forEach(function (commit) {
      var b = commit.benches.find(function (x) { return x.name === benchName; });
      if (b) {
        series.push({
          sha: commit.commit.id.substring(0, 7),
          val: b.value,
          msg: commit.commit.message.split('\n')[0]
        });
      }
    });
    return series;
  }

  // ── Chart creation ──

  function makeChart(canvas, series, unit, large) {
    return new Chart(canvas, {
      type: 'line',
      data: {
        labels: series.map(function (s) { return s.sha; }),
        datasets: [{
          data: series.map(function (s) { return s.val / unit.d; }),
          borderColor: COLORS[0],
          backgroundColor: COLORS[0] + '18',
          fill: true,
          tension: 0.3,
          pointRadius: large ? 4 : 3,
          pointHoverRadius: large ? 7 : 5,
          borderWidth: 2
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 250 },
        plugins: {
          legend: { display: false },
          tooltip: {
            titleFont: { size: 14, weight: 'bold' },
            bodyFont: { size: 13 },
            callbacks: {
              title: function (ctx) { return series[ctx[0].dataIndex].msg; },
              label: function (ctx) { return ctx.parsed.y.toFixed(2) + ' ' + unit.l; }
            }
          }
        },
        scales: {
          x: { ticks: { font: { size: large ? 12 : 10 }, maxTicksLimit: large ? 15 : 8 } },
          y: {
            title: { display: true, text: unit.l, font: { size: 13, weight: 'bold' } },
            ticks: { font: { size: large ? 12 : 11 } },
            beginAtZero: false
          }
        }
      }
    });
  }

  // ── Fullscreen modal ──

  var modal = null;
  var modalChart = null;

  function initModal() {
    modal = document.createElement('div');
    modal.className = 'modal-overlay';
    modal.innerHTML =
      '<div class="modal-content">' +
      '  <div class="modal-header">' +
      '    <h3 id="modal-title"></h3>' +
      '    <button class="modal-close">\u00d7</button>' +
      '  </div>' +
      '  <div class="modal-chart"><canvas id="modal-canvas"></canvas></div>' +
      '</div>';
    document.body.appendChild(modal);
    modal.querySelector('.modal-close').onclick = closeModal;
    modal.onclick = function (e) { if (e.target === modal) closeModal(); };
    document.addEventListener('keydown', function (e) { if (e.key === 'Escape') closeModal(); });
  }

  function openModal(title, series, unit) {
    if (!modal) initModal();
    document.getElementById('modal-title').textContent = title;
    modal.classList.add('active');
    document.body.style.overflow = 'hidden';
    if (modalChart) modalChart.destroy();
    modalChart = makeChart(document.getElementById('modal-canvas'), series, unit, true);
  }

  function closeModal() {
    if (!modal) return;
    modal.classList.remove('active');
    document.body.style.overflow = '';
    if (modalChart) { modalChart.destroy(); modalChart = null; }
  }

  // ── Hash navigation ──

  function setView(hash) {
    var sections = document.querySelectorAll('.view-section');
    for (var i = 0; i < sections.length; i++) sections[i].classList.remove('active');
    var target = hash ? document.getElementById('view-' + hash) : null;
    (target || document.getElementById('view-overview')).classList.add('active');
    window.scrollTo(0, 0);
  }

  // ── Main renderer ──

  window.renderOptimizer = function (containerId) {
    var data = getData();
    var el = document.getElementById(containerId);

    if (!data.length) {
      el.innerHTML = '<p class="empty">No benchmark data yet. Push to main to generate.</p>';
      return;
    }

    // Discover optimizer name from benchmark data (fully dynamic)
    var firstName = data[data.length - 1].benches[0].name;
    var optimizerName = firstName.split('/')[0];
    var titleEl = document.getElementById('optimizer-title');
    var pageEl = document.getElementById('page-title');
    if (titleEl) titleEl.textContent = optimizerName;
    if (pageEl) pageEl.textContent = optimizerName;
    document.title = optimizerName + ' \u2014 Vimficiency Benchmarks';

    var cats = discoverCategories(data);

    // ── Overview section ──
    var overview = document.createElement('div');
    overview.id = 'view-overview';
    overview.className = 'view-section active';

    var h = document.createElement('h2');
    h.textContent = 'Categories';
    overview.appendChild(h);

    var grid = document.createElement('div');
    grid.className = 'summary-grid';

    var catEntries = Object.keys(cats).map(function (k) { return [k, cats[k]]; });

    catEntries.forEach(function (entry) {
      var catName = entry[0];
      var benchNames = entry[1];
      var latest = data[data.length - 1];
      var prev = data.length > 1 ? data[data.length - 2] : null;

      var lSum = 0, pSum = 0, cnt = 0;
      benchNames.forEach(function (name) {
        var lb = latest.benches.find(function (b) { return b.name === name; });
        if (lb) { lSum += lb.value; cnt++; }
        if (prev) {
          var pb = prev.benches.find(function (b) { return b.name === name; });
          if (pb) pSum += pb.value;
        }
      });

      var avg = cnt ? lSum / cnt : 0;
      var trend = (prev && pSum > 0) ? (lSum - pSum) / pSum * 100 : null;

      var card = document.createElement('a');
      card.href = '#' + catName;
      card.className = 'summary-card';

      var trendHtml = '';
      if (trend !== null) {
        var cls = trend > 5 ? 'trend-bad' : trend < -5 ? 'trend-good' : 'trend-neutral';
        trendHtml = ' \u00b7 <span class="' + cls + '">' +
          (trend > 0 ? '+' : '') + trend.toFixed(1) + '%</span>';
      }

      card.innerHTML =
        '<div class="card-title">' + catName + '</div>' +
        '<div class="card-value">' + fmtTime(avg) + '</div>' +
        '<div class="card-meta">' + cnt + ' benchmark' + (cnt !== 1 ? 's' : '') + trendHtml + '</div>';

      grid.appendChild(card);
    });

    overview.appendChild(grid);
    el.appendChild(overview);

    // ── Per-category sections ──
    catEntries.forEach(function (entry) {
      var catName = entry[0];
      var benchNames = entry[1];

      var sec = document.createElement('div');
      sec.id = 'view-' + catName;
      sec.className = 'view-section';

      var header = document.createElement('div');
      header.className = 'category-header';
      header.innerHTML = '<a href="#" class="back-link">\u2190 All categories</a><h2>' + catName + '</h2>';
      sec.appendChild(header);

      var chartGrid = document.createElement('div');
      chartGrid.className = 'chart-grid';

      benchNames.forEach(function (benchName) {
        var detail = parseName(benchName).detail;
        var series = timeSeries(data, benchName);
        if (!series.length) return;

        var unit = bestUnit(series.map(function (s) { return s.val; }));

        var card = document.createElement('div');
        card.className = 'chart-card';
        card.innerHTML = '<h4>' + detail + '</h4><div class="chart-wrapper"><canvas></canvas></div>';
        chartGrid.appendChild(card);

        // Render after DOM insertion
        requestAnimationFrame(function () {
          makeChart(card.querySelector('canvas'), series, unit, false);
          card.onclick = function () { openModal(detail, series, unit); };
        });
      });

      sec.appendChild(chartGrid);
      el.appendChild(sec);
    });

    // ── Activate initial view ──
    setView(location.hash.substring(1));
    window.addEventListener('hashchange', function () {
      setView(location.hash.substring(1));
    });
  };
})();
