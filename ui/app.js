/* ════════════════════════════════════════════════════════════
   KMDD Dashboard — Frontend Application
   ════════════════════════════════════════════════════════════ */

(() => {
    'use strict';

    // ── Configuration ─────────────────────────────────────────
    const CONFIG = {
        wsUrl: 'ws://localhost:8765',
        maxEvents: 200,
        reconnectDelay: 2000,
        reconnectMaxDelay: 10000,
        autoScroll: true,
        showSyn: false,
    };

    // ── State ─────────────────────────────────────────────────
    const state = {
        ws: null,
        connected: false,
        reconnectAttempts: 0,
        eventCount: 0,
        keyPressCount: 0,
        mouseEventCount: 0,
        clickCount: 0,
        scrollTotal: 0,
        keyFrequency: {},
        eventsPerSecond: [],
        startTime: Date.now(),
        pressedKeys: new Set(),
        mouseTrail: [],
        mouseCursorX: 250,
        mouseCursorY: 200,
        // Week 4 additions
        buttonClicks: { left: 0, middle: 0, right: 0 },
        mouseDistance: 0,
        waypoints: [],
        touchX: 0,
        touchY: 0,
        touching: false,
        touchTrail: [],
    };

    // ── DOM References ────────────────────────────────────────
    const dom = {
        connectionStatus: document.getElementById('connection-status'),
        connLabel: null,
        eventCounter: document.getElementById('event-counter'),
        counterValue: null,
        eventFeed: document.getElementById('event-feed'),
        tabNav: document.getElementById('tab-nav'),
        panels: document.querySelectorAll('.panel'),
        tabs: document.querySelectorAll('.tab'),
        btnAutoScroll: document.getElementById('btn-auto-scroll'),
        btnClearEvents: document.getElementById('btn-clear-events'),
        btnReconnect: document.getElementById('btn-reconnect'),
        mouseCanvas: document.getElementById('mouse-canvas'),
        mouseDx: document.getElementById('mouse-dx'),
        mouseDy: document.getElementById('mouse-dy'),
        scrollValue: document.getElementById('scroll-value'),
        statKeyCount: document.getElementById('stat-key-count'),
        statMouseCount: document.getElementById('stat-mouse-count'),
        statClickCount: document.getElementById('stat-click-count'),
        statUptime: document.getElementById('stat-uptime'),
        statDistance: document.getElementById('stat-distance'),
        topKeys: document.getElementById('top-keys'),
        epsFill: document.getElementById('eps-fill'),
        epsLabel: document.getElementById('eps-label'),
        pieChart: document.getElementById('pie-chart'),
        wsUrlInput: document.getElementById('ws-url'),
        maxEventsInput: document.getElementById('max-events'),
        showSynInput: document.getElementById('show-syn'),
        autoReconnectInput: document.getElementById('ws-auto-reconnect'),
        // Injection panel
        injectTextInput: document.getElementById('inject-text-input'),
        btnInjectText: document.getElementById('btn-inject-text'),
        injectTextStatus: document.getElementById('inject-text-status'),
        injectPresetStatus: document.getElementById('inject-preset-status'),
        waypointCanvas: document.getElementById('waypoint-canvas'),
        btnInjectPath: document.getElementById('btn-inject-path'),
        btnClearWaypoints: document.getElementById('btn-clear-waypoints'),
        waypointCount: document.getElementById('waypoint-count'),
        injectPathStatus: document.getElementById('inject-path-status'),
        // Touchpad
        touchpadCanvas: document.getElementById('touchpad-canvas'),
        touchX: document.getElementById('touch-x'),
        touchY: document.getElementById('touch-y'),
        touchStateLabel: document.getElementById('touch-state-label'),
        touchIndicator: document.getElementById('touch-indicator'),
        gestureLog: document.getElementById('gesture-log'),
        // Advanced settings
        repeatDelay: document.getElementById('repeat-delay'),
        repeatDelayVal: document.getElementById('repeat-delay-val'),
        repeatRate: document.getElementById('repeat-rate'),
        repeatRateVal: document.getElementById('repeat-rate-val'),
        dpiMultiplier: document.getElementById('dpi-multiplier'),
        dpiVal: document.getElementById('dpi-val'),
        ledCaps: document.getElementById('led-caps'),
        ledNum: document.getElementById('led-num'),
        ledScroll: document.getElementById('led-scroll'),
        logLevel: document.getElementById('log-level'),
    };

    // Grab sub-elements
    dom.connLabel = dom.connectionStatus.querySelector('.conn-label');
    dom.counterValue = dom.eventCounter.querySelector('.counter-value');

    // ── WebSocket Manager ─────────────────────────────────────
    function connect() {
        const url = dom.wsUrlInput ? dom.wsUrlInput.value : CONFIG.wsUrl;

        try {
            state.ws = new WebSocket(url);
        } catch (e) {
            setConnectionStatus('disconnected', 'Error');
            return;
        }

        state.ws.onopen = () => {
            state.connected = true;
            state.reconnectAttempts = 0;
            setConnectionStatus('connected', 'Connected');
            console.log('[KMDD] WebSocket connected');
        };

        state.ws.onmessage = (evt) => {
            try {
                const data = JSON.parse(evt.data);
                handleMessage(data);
            } catch (e) {
                console.warn('[KMDD] Bad message:', evt.data);
            }
        };

        state.ws.onclose = () => {
            state.connected = false;
            setConnectionStatus('disconnected', 'Disconnected');
            console.log('[KMDD] WebSocket closed');
            scheduleReconnect();
        };

        state.ws.onerror = () => {
            state.connected = false;
            setConnectionStatus('disconnected', 'Error');
        };
    }

    function scheduleReconnect() {
        const autoReconnect = dom.autoReconnectInput ? dom.autoReconnectInput.checked : true;
        if (!autoReconnect) return;

        state.reconnectAttempts++;
        const delay = Math.min(
            CONFIG.reconnectDelay * state.reconnectAttempts,
            CONFIG.reconnectMaxDelay
        );
        setConnectionStatus('disconnected', `Reconnecting in ${(delay / 1000).toFixed(0)}s…`);
        setTimeout(connect, delay);
    }

    function setConnectionStatus(status, label) {
        dom.connectionStatus.className = 'connection-status ' + status;
        dom.connLabel.textContent = label;
    }

    function sendCommand(cmd) {
        if (state.ws && state.ws.readyState === WebSocket.OPEN) {
            state.ws.send(JSON.stringify(cmd));
        }
    }

    // ── Message Handler ───────────────────────────────────────
    function handleMessage(data) {
        if (data.type === 'status') {
            handleStatus(data);
            return;
        }

        if (data.type === 'inject_result') {
            console.log('[KMDD] Inject:', data.message);
            return;
        }

        if (data.type === 'error') {
            console.error('[KMDD] Server error:', data.message);
            return;
        }

        // Input event
        handleEvent(data);
    }

    function handleStatus(data) {
        // Update module status pills
        const devices = data.devices || [];
        const pills = {
            keyboard: document.getElementById('pill-keyboard'),
            mouse: document.getElementById('pill-mouse'),
            touchpad: document.getElementById('pill-touchpad'),
        };

        // In simulation mode, show all as active
        if (data.simulate) {
            Object.values(pills).forEach(p => p.classList.add('active'));
        }
    }

    // ── Event Processing ──────────────────────────────────────
    function handleEvent(ev) {
        state.eventCount++;
        dom.counterValue.textContent = state.eventCount.toLocaleString();

        // Track EPS
        state.eventsPerSecond.push(Date.now());

        // Process by type
        if (ev.type === 'KEY') {
            handleKeyEvent(ev);
        } else if (ev.type === 'REL') {
            handleRelEvent(ev);
        } else if (ev.type === 'ABS') {
            handleAbsEvent(ev);
        }

        // Add to live feed
        addEventToFeed(ev);

        // Update stats
        updateStats();
    }

    function handleKeyEvent(ev) {
        const code = ev.code;
        const isMouseBtn = code >= 272 && code < 288;

        if (ev.action === 'press') {
            state.pressedKeys.add(code);

            if (isMouseBtn) {
                state.clickCount++;
                setMouseButton(code, true);
                trackButtonClick(code);
            } else {
                state.keyPressCount++;
                // Track frequency
                const keyName = ev.key || `KEY_${code}`;
                state.keyFrequency[keyName] = (state.keyFrequency[keyName] || 0) + 1;
            }

            // Highlight keyboard key
            highlightKey(code, true);
        } else if (ev.action === 'release') {
            state.pressedKeys.delete(code);

            if (isMouseBtn) {
                setMouseButton(code, false);
            }

            // Un-highlight after a short delay for visual effect
            setTimeout(() => highlightKey(code, false), 150);
        }
    }

    function handleRelEvent(ev) {
        state.mouseEventCount++;

        if (ev.axis === 'X') {
            dom.mouseDx.textContent = ev.value > 0 ? `+${ev.value}` : ev.value;
            state.mouseCursorX = Math.max(5, Math.min(495, state.mouseCursorX + ev.value));
            state.mouseDistance += Math.abs(ev.value);
        } else if (ev.axis === 'Y') {
            dom.mouseDy.textContent = ev.value > 0 ? `+${ev.value}` : ev.value;
            state.mouseCursorY = Math.max(5, Math.min(395, state.mouseCursorY + ev.value));
            state.mouseDistance += Math.abs(ev.value);
        } else if (ev.axis === 'WHEEL') {
            state.scrollTotal += ev.value;
            dom.scrollValue.textContent = ev.value > 0 ? `↑${ev.value}` : `↓${Math.abs(ev.value)}`;
        }

        // Add to trail
        state.mouseTrail.push({
            x: state.mouseCursorX,
            y: state.mouseCursorY,
            time: Date.now()
        });
        if (state.mouseTrail.length > 200) {
            state.mouseTrail.shift();
        }

        drawMouseCanvas();
    }

    // ── Touchpad Visualization ─────────────────────────────────
    function handleAbsEvent(ev) {
        if (ev.axis === 'X') {
            state.touchX = ev.value;
            if (dom.touchX) dom.touchX.textContent = ev.value;
        } else if (ev.axis === 'Y') {
            state.touchY = ev.value;
            if (dom.touchY) dom.touchY.textContent = ev.value;
        }
        state.touching = true;
        if (dom.touchStateLabel) dom.touchStateLabel.textContent = 'Touching';
        if (dom.touchIndicator) dom.touchIndicator.classList.add('active');

        state.touchTrail.push({ x: state.touchX, y: state.touchY, time: Date.now() });
        if (state.touchTrail.length > 150) state.touchTrail.shift();

        drawTouchpadCanvas();
    }

    function drawTouchpadCanvas() {
        const canvas = dom.touchpadCanvas;
        if (!canvas) return;
        const ctx = canvas.getContext('2d');
        const W = canvas.width, H = canvas.height;

        // Background
        ctx.fillStyle = '#0f172a';
        ctx.fillRect(0, 0, W, H);

        // Grid
        ctx.strokeStyle = 'rgba(99,102,241,0.15)';
        ctx.lineWidth = 1;
        for (let x = 0; x < W; x += 40) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke(); }
        for (let y = 0; y < H; y += 40) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke(); }

        // Trail
        if (state.touchTrail.length > 1) {
            const now = Date.now();
            ctx.lineWidth = 2;
            for (let i = 1; i < state.touchTrail.length; i++) {
                const p = state.touchTrail[i];
                const age = (now - p.time) / 3000;
                if (age > 1) continue;
                const px = (state.touchTrail[i - 1].x / 4096) * W;
                const py = (state.touchTrail[i - 1].y / 4096) * H;
                const cx = (p.x / 4096) * W;
                const cy = (p.y / 4096) * H;
                ctx.strokeStyle = `rgba(168,85,247,${1 - age})`;
                ctx.beginPath(); ctx.moveTo(px, py); ctx.lineTo(cx, cy); ctx.stroke();
            }
        }

        // Touch point
        if (state.touching) {
            const cx = (state.touchX / 4096) * W;
            const cy = (state.touchY / 4096) * H;
            const grad = ctx.createRadialGradient(cx, cy, 0, cx, cy, 20);
            grad.addColorStop(0, 'rgba(168,85,247,0.9)');
            grad.addColorStop(1, 'rgba(168,85,247,0)');
            ctx.fillStyle = grad;
            ctx.beginPath(); ctx.arc(cx, cy, 20, 0, Math.PI * 2); ctx.fill();
            ctx.fillStyle = '#a855f7';
            ctx.beginPath(); ctx.arc(cx, cy, 5, 0, Math.PI * 2); ctx.fill();
        }

        // Border
        ctx.strokeStyle = 'rgba(99,102,241,0.3)';
        ctx.lineWidth = 2;
        ctx.strokeRect(1, 1, W - 2, H - 2);
    }

    // ── Virtual Keyboard ──────────────────────────────────────
    function highlightKey(code, pressed) {
        const keyEl = document.querySelector(`.key[data-code="${code}"]`);
        if (keyEl) {
            if (pressed) {
                keyEl.classList.add('pressed');
            } else {
                keyEl.classList.remove('pressed');
            }
        }
    }

    function setupKeyboardClicks() {
        document.querySelectorAll('.key[data-scan]').forEach(key => {
            key.addEventListener('click', () => {
                const scan = key.getAttribute('data-scan');
                if (scan) {
                    sendCommand({ action: 'inject_key', scancode: scan });

                    // Visual feedback
                    key.classList.add('pressed');
                    setTimeout(() => key.classList.remove('pressed'), 200);
                }
            });
        });
    }

    // ── Mouse Visualization ───────────────────────────────────
    function setMouseButton(code, active) {
        const btnMap = { 272: 'mbtn-left', 273: 'mbtn-right', 274: 'mbtn-middle' };
        const el = document.getElementById(btnMap[code]);
        if (el) {
            el.classList.toggle('active', active);
        }
    }

    function drawMouseCanvas() {
        const canvas = dom.mouseCanvas;
        if (!canvas) return;
        const ctx = canvas.getContext('2d');
        const w = canvas.width;
        const h = canvas.height;

        ctx.clearRect(0, 0, w, h);

        // Draw grid
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.03)';
        ctx.lineWidth = 1;
        for (let x = 0; x < w; x += 25) {
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, h);
            ctx.stroke();
        }
        for (let y = 0; y < h; y += 25) {
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(w, y);
            ctx.stroke();
        }

        // Draw trail
        const now = Date.now();
        const trail = state.mouseTrail;
        if (trail.length > 1) {
            for (let i = 1; i < trail.length; i++) {
                const age = (now - trail[i].time) / 3000; // 3 second fade
                const alpha = Math.max(0, 1 - age);
                if (alpha <= 0) continue;

                ctx.beginPath();
                ctx.moveTo(trail[i - 1].x, trail[i - 1].y);
                ctx.lineTo(trail[i].x, trail[i].y);
                ctx.strokeStyle = `rgba(59, 130, 246, ${alpha * 0.6})`;
                ctx.lineWidth = 2;
                ctx.stroke();
            }
        }

        // Draw cursor
        const cx = state.mouseCursorX;
        const cy = state.mouseCursorY;

        // Glow
        const grad = ctx.createRadialGradient(cx, cy, 0, cx, cy, 15);
        grad.addColorStop(0, 'rgba(59, 130, 246, 0.4)');
        grad.addColorStop(1, 'rgba(59, 130, 246, 0)');
        ctx.fillStyle = grad;
        ctx.beginPath();
        ctx.arc(cx, cy, 15, 0, Math.PI * 2);
        ctx.fill();

        // Dot
        ctx.fillStyle = '#3b82f6';
        ctx.beginPath();
        ctx.arc(cx, cy, 4, 0, Math.PI * 2);
        ctx.fill();

        // Crosshair
        ctx.strokeStyle = 'rgba(59, 130, 246, 0.3)';
        ctx.lineWidth = 1;
        ctx.setLineDash([4, 4]);
        ctx.beginPath();
        ctx.moveTo(cx, 0); ctx.lineTo(cx, h);
        ctx.moveTo(0, cy); ctx.lineTo(w, cy);
        ctx.stroke();
        ctx.setLineDash([]);
    }

    // ── Live Event Feed ───────────────────────────────────────
    function addEventToFeed(ev) {
        // Remove empty state message
        const empty = dom.eventFeed.querySelector('.event-empty');
        if (empty) empty.remove();

        // Create event row
        const row = document.createElement('div');
        row.className = 'event-row';

        // Badge class
        let badgeClass = 'badge-key';
        if (ev.type === 'REL') badgeClass = 'badge-rel';
        else if (ev.type === 'ABS') badgeClass = 'badge-abs';
        else if (ev.type === 'LED') badgeClass = 'badge-led';

        // Detail text
        let detail = '';
        let actionHtml = '';

        if (ev.type === 'KEY') {
            detail = ev.key || `code=${ev.code}`;
            const actionClass = ev.action === 'press' ? 'action-press' :
                ev.action === 'release' ? 'action-release' : 'action-repeat';
            const actionSymbol = ev.action === 'press' ? '▼' :
                ev.action === 'release' ? '▲' : '↻';
            actionHtml = `<span class="event-action ${actionClass}">${actionSymbol} ${ev.action}</span>`;
        } else if (ev.type === 'REL') {
            detail = `${ev.axis}: ${ev.value > 0 ? '+' : ''}${ev.value}`;
        } else if (ev.type === 'ABS') {
            detail = `${ev.axis}: ${ev.value}`;
        } else {
            detail = `code=${ev.code} value=${ev.value}`;
        }

        row.innerHTML = `
            <span class="event-id">#${ev.id}</span>
            <span class="event-time">${ev.time}</span>
            <span class="event-badge ${badgeClass}">${ev.type}</span>
            <span class="event-detail">${detail}</span>
            ${actionHtml}
        `;

        dom.eventFeed.appendChild(row);

        // Trim excess events
        const maxEvents = parseInt(dom.maxEventsInput?.value) || CONFIG.maxEvents;
        while (dom.eventFeed.children.length > maxEvents) {
            dom.eventFeed.removeChild(dom.eventFeed.firstChild);
        }

        // Auto-scroll
        if (CONFIG.autoScroll) {
            dom.eventFeed.scrollTop = dom.eventFeed.scrollHeight;
        }
    }

    // ── Statistics ─────────────────────────────────────────────
    function updateStats() {
        dom.statKeyCount.textContent = state.keyPressCount.toLocaleString();
        dom.statMouseCount.textContent = state.mouseEventCount.toLocaleString();
        dom.statClickCount.textContent = state.clickCount.toLocaleString();
        if (dom.statDistance) dom.statDistance.textContent = Math.round(state.mouseDistance).toLocaleString();

        // Uptime
        const uptime = Math.floor((Date.now() - state.startTime) / 1000);
        if (uptime < 60) {
            dom.statUptime.textContent = `${uptime}s`;
        } else if (uptime < 3600) {
            dom.statUptime.textContent = `${Math.floor(uptime / 60)}m ${uptime % 60}s`;
        } else {
            dom.statUptime.textContent = `${Math.floor(uptime / 3600)}h ${Math.floor((uptime % 3600) / 60)}m`;
        }

        // Top keys
        updateTopKeys();

        // EPS
        updateEPS();

        // Pie chart
        drawPieChart();
    }

    function updateTopKeys() {
        const sorted = Object.entries(state.keyFrequency)
            .sort((a, b) => b[1] - a[1])
            .slice(0, 8);

        if (sorted.length === 0) return;

        dom.topKeys.innerHTML = sorted.map(([key, count]) =>
            `<div class="top-key-chip">
                <span class="top-key-name">${key}</span>
                <span class="top-key-count">×${count}</span>
            </div>`
        ).join('');
    }

    function updateEPS() {
        const now = Date.now();
        state.eventsPerSecond = state.eventsPerSecond.filter(t => now - t < 2000);
        const eps = state.eventsPerSecond.length / 2;

        const maxEps = 50;
        const pct = Math.min(100, (eps / maxEps) * 100);
        dom.epsFill.style.width = `${pct}%`;
        dom.epsLabel.textContent = `${eps.toFixed(1)} e/s`;
    }

    // ── Pie Chart ──────────────────────────────────────────────
    function drawPieChart() {
        const canvas = dom.pieChart;
        if (!canvas) return;
        const ctx = canvas.getContext('2d');
        const W = canvas.width, H = canvas.height;
        const cx = W / 2, cy = H / 2, r = Math.min(cx, cy) - 15;

        ctx.clearRect(0, 0, W, H);

        const data = [
            { label: 'Left', value: state.buttonClicks.left, color: '#3b82f6' },
            { label: 'Middle', value: state.buttonClicks.middle, color: '#10b981' },
            { label: 'Right', value: state.buttonClicks.right, color: '#f59e0b' },
        ];
        const total = data.reduce((s, d) => s + d.value, 0);

        if (total === 0) {
            // Draw empty state
            ctx.strokeStyle = 'rgba(255,255,255,0.1)';
            ctx.lineWidth = 3;
            ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.stroke();
            ctx.fillStyle = 'rgba(255,255,255,0.3)';
            ctx.font = '13px Inter, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText('No clicks yet', cx, cy + 5);
            return;
        }

        let startAngle = -Math.PI / 2;
        data.forEach(d => {
            if (d.value === 0) return;
            const sliceAngle = (d.value / total) * Math.PI * 2;
            ctx.fillStyle = d.color;
            ctx.beginPath();
            ctx.moveTo(cx, cy);
            ctx.arc(cx, cy, r, startAngle, startAngle + sliceAngle);
            ctx.closePath();
            ctx.fill();

            // Label
            const midAngle = startAngle + sliceAngle / 2;
            const lx = cx + (r * 0.6) * Math.cos(midAngle);
            const ly = cy + (r * 0.6) * Math.sin(midAngle);
            ctx.fillStyle = '#fff';
            ctx.font = 'bold 12px Inter, sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            const pct = ((d.value / total) * 100).toFixed(0);
            ctx.fillText(`${pct}%`, lx, ly);

            startAngle += sliceAngle;
        });

        // Center hole (donut)
        ctx.fillStyle = '#0f172a';
        ctx.beginPath(); ctx.arc(cx, cy, r * 0.45, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = '#e2e8f0';
        ctx.font = 'bold 16px Inter, sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(total.toString(), cx, cy);
    }

    // ── Tab Navigation ────────────────────────────────────────
    function setupTabs() {
        dom.tabs.forEach(tab => {
            tab.addEventListener('click', () => {
                const target = tab.getAttribute('data-tab');

                // Deactivate all
                dom.tabs.forEach(t => t.classList.remove('active'));
                dom.panels.forEach(p => p.classList.remove('active'));

                // Activate target
                tab.classList.add('active');
                const panel = document.getElementById(`panel-${target}`);
                if (panel) panel.classList.add('active');

                // Redraw canvas when switching to mouse tab
                if (target === 'mouse') {
                    drawMouseCanvas();
                }
            });
        });
    }

    // ── Control Buttons ───────────────────────────────────────
    function setupControls() {
        // Auto-scroll toggle
        dom.btnAutoScroll?.addEventListener('click', () => {
            CONFIG.autoScroll = !CONFIG.autoScroll;
            dom.btnAutoScroll.style.opacity = CONFIG.autoScroll ? '1' : '0.5';
        });

        // Clear events
        dom.btnClearEvents?.addEventListener('click', () => {
            dom.eventFeed.innerHTML = `
                <div class="event-empty">
                    <span class="empty-icon">📡</span>
                    <p>Event feed cleared.</p>
                    <p class="empty-hint">New events will appear here.</p>
                </div>
            `;
        });

        // Reconnect
        dom.btnReconnect?.addEventListener('click', () => {
            if (state.ws) {
                state.ws.close();
            }
            state.reconnectAttempts = 0;
            connect();
        });

        // Settings — basic
        dom.wsUrlInput?.addEventListener('change', () => {
            CONFIG.wsUrl = dom.wsUrlInput.value;
        });

        dom.maxEventsInput?.addEventListener('change', () => {
            CONFIG.maxEvents = parseInt(dom.maxEventsInput.value) || 200;
        });
    }

    // ── Injection Controls ────────────────────────────────────
    function setupInjection() {
        // Text injection
        dom.btnInjectText?.addEventListener('click', () => {
            const text = dom.injectTextInput?.value || '';
            if (!text) return;
            sendCommand({ action: 'inject_text', text });
            showInjectStatus(dom.injectTextStatus, `Sending "${text}"…`);
            dom.injectTextInput.value = '';
        });

        dom.injectTextInput?.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') dom.btnInjectText?.click();
        });

        // Preset buttons
        document.querySelectorAll('.preset-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                const preset = btn.dataset.preset;
                sendCommand({ action: 'inject_preset', preset });
                showInjectStatus(dom.injectPresetStatus, `Running "${preset}"…`);
            });
        });

        // Waypoint canvas
        const wpCanvas = dom.waypointCanvas;
        if (wpCanvas) {
            wpCanvas.addEventListener('click', (e) => {
                const rect = wpCanvas.getBoundingClientRect();
                const x = Math.round(e.clientX - rect.left);
                const y = Math.round(e.clientY - rect.top);
                state.waypoints.push({ x, y });
                drawWaypointCanvas();
            });
        }

        dom.btnInjectPath?.addEventListener('click', () => {
            if (state.waypoints.length < 2) {
                showInjectStatus(dom.injectPathStatus, 'Need at least 2 waypoints');
                return;
            }
            sendCommand({ action: 'inject_path', waypoints: state.waypoints });
            showInjectStatus(dom.injectPathStatus, `Injecting ${state.waypoints.length} waypoints…`);
        });

        dom.btnClearWaypoints?.addEventListener('click', () => {
            state.waypoints = [];
            drawWaypointCanvas();
        });
    }

    function showInjectStatus(el, msg) {
        if (!el) return;
        el.textContent = msg;
        el.classList.add('show');
        setTimeout(() => el.classList.remove('show'), 3000);
    }

    function drawWaypointCanvas() {
        const canvas = dom.waypointCanvas;
        if (!canvas) return;
        const ctx = canvas.getContext('2d');
        const W = canvas.width, H = canvas.height;

        ctx.fillStyle = '#0f172a';
        ctx.fillRect(0, 0, W, H);

        // Grid
        ctx.strokeStyle = 'rgba(99,102,241,0.12)';
        ctx.lineWidth = 1;
        for (let x = 0; x < W; x += 25) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke(); }
        for (let y = 0; y < H; y += 25) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke(); }

        // Draw path
        if (state.waypoints.length > 1) {
            ctx.strokeStyle = '#6366f1';
            ctx.lineWidth = 2;
            ctx.setLineDash([5, 5]);
            ctx.beginPath();
            ctx.moveTo(state.waypoints[0].x, state.waypoints[0].y);
            for (let i = 1; i < state.waypoints.length; i++) {
                ctx.lineTo(state.waypoints[i].x, state.waypoints[i].y);
            }
            ctx.stroke();
            ctx.setLineDash([]);
        }

        // Draw points
        state.waypoints.forEach((wp, i) => {
            ctx.fillStyle = i === 0 ? '#10b981' : i === state.waypoints.length - 1 ? '#ef4444' : '#6366f1';
            ctx.beginPath(); ctx.arc(wp.x, wp.y, 6, 0, Math.PI * 2); ctx.fill();
            ctx.fillStyle = '#fff';
            ctx.font = '10px Inter, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(i + 1, wp.x, wp.y - 10);
        });

        // Border
        ctx.strokeStyle = 'rgba(99,102,241,0.3)';
        ctx.lineWidth = 2;
        ctx.strokeRect(1, 1, W - 2, H - 2);

        if (dom.waypointCount) dom.waypointCount.textContent = `${state.waypoints.length} waypoints`;
    }

    // ── Advanced Settings ─────────────────────────────────────
    function setupAdvancedSettings() {
        // Repeat delay slider
        dom.repeatDelay?.addEventListener('input', () => {
            const v = dom.repeatDelay.value;
            if (dom.repeatDelayVal) dom.repeatDelayVal.textContent = `${v}ms`;
        });
        dom.repeatDelay?.addEventListener('change', () => {
            sendCommand({ action: 'set_repeat', delay: parseInt(dom.repeatDelay.value) });
        });

        // Repeat rate slider
        dom.repeatRate?.addEventListener('input', () => {
            const v = dom.repeatRate.value;
            if (dom.repeatRateVal) dom.repeatRateVal.textContent = `${v}ms`;
        });
        dom.repeatRate?.addEventListener('change', () => {
            sendCommand({ action: 'set_repeat', rate: parseInt(dom.repeatRate.value) });
        });

        // DPI slider
        dom.dpiMultiplier?.addEventListener('input', () => {
            const v = dom.dpiMultiplier.value;
            if (dom.dpiVal) dom.dpiVal.textContent = `${v}x`;
        });
        dom.dpiMultiplier?.addEventListener('change', () => {
            sendCommand({ action: 'set_dpi', dpi: parseInt(dom.dpiMultiplier.value) });
        });

        // LED toggles
        ['caps', 'num', 'scroll'].forEach(led => {
            const el = dom[`led${led.charAt(0).toUpperCase() + led.slice(1)}`];
            el?.addEventListener('change', () => {
                sendCommand({ action: 'set_led', led, state: el.checked ? 1 : 0 });
            });
        });

        // Log level
        dom.logLevel?.addEventListener('change', () => {
            sendCommand({ action: 'set_log_level', level: dom.logLevel.value });
        });
    }

    // ── Button Click Tracking ─────────────────────────────────
    function trackButtonClick(code) {
        if (code === 272) state.buttonClicks.left++;
        else if (code === 273) state.buttonClicks.right++;
        else if (code === 274) state.buttonClicks.middle++;
    }

    // ── Periodic Updates ──────────────────────────────────────
    function startPeriodicUpdates() {
        setInterval(() => {
            updateEPS();
            if (dom.statDistance) dom.statDistance.textContent = Math.round(state.mouseDistance).toLocaleString();

            const uptime = Math.floor((Date.now() - state.startTime) / 1000);
            if (uptime < 60) {
                dom.statUptime.textContent = `${uptime}s`;
            } else if (uptime < 3600) {
                dom.statUptime.textContent = `${Math.floor(uptime / 60)}m ${uptime % 60}s`;
            } else {
                dom.statUptime.textContent = `${Math.floor(uptime / 3600)}h ${Math.floor((uptime % 3600) / 60)}m`;
            }
        }, 1000);

        // Clean old trail points every 5 seconds
        setInterval(() => {
            const now = Date.now();
            state.mouseTrail = state.mouseTrail.filter(p => now - p.time < 5000);
            state.touchTrail = state.touchTrail.filter(p => now - p.time < 5000);
        }, 5000);
    }

    // ── Initialization ────────────────────────────────────────
    function init() {
        console.log('[KMDD] Dashboard initializing…');

        setupTabs();
        setupControls();
        setupKeyboardClicks();
        setupInjection();
        setupAdvancedSettings();
        startPeriodicUpdates();

        // Initial canvas draws
        drawMouseCanvas();
        drawTouchpadCanvas();
        drawWaypointCanvas();
        drawPieChart();

        // Connect WebSocket
        connect();

        console.log('[KMDD] Dashboard ready ✓');
    }

    // Start
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
