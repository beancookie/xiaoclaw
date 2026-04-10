// ========================================
// XiaoClaw - Landing Page JavaScript
// ========================================

(function() {
    'use strict';

    // ========================================
    // Particles Background
    // ========================================
    class Particles {
        constructor() {
            this.canvas = document.getElementById('particles');
            this.ctx = this.canvas.getContext('2d');
            this.particles = [];
            this.particleCount = 80;
            this.mouse = { x: null, y: null, radius: 150 };

            this.resize();
            this.init();
            this.animate();

            window.addEventListener('resize', () => this.resize());
            window.addEventListener('mousemove', (e) => {
                this.mouse.x = e.x;
                this.mouse.y = e.y;
            });
        }

        resize() {
            this.canvas.width = window.innerWidth;
            this.canvas.height = window.innerHeight;
        }

        init() {
            this.particles = [];
            for (let i = 0; i < this.particleCount; i++) {
                this.particles.push({
                    x: Math.random() * this.canvas.width,
                    y: Math.random() * this.canvas.height,
                    size: Math.random() * 2 + 1,
                    speedX: (Math.random() - 0.5) * 0.5,
                    speedY: (Math.random() - 0.5) * 0.5,
                    color: Math.random() > 0.5 ? '#00d4ff' : '#7c3aed'
                });
            }
        }

        animate() {
            this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

            this.particles.forEach((p, i) => {
                // Move
                p.x += p.speedX;
                p.y += p.speedY;

                // Boundary check
                if (p.x < 0 || p.x > this.canvas.width) p.speedX *= -1;
                if (p.y < 0 || p.y > this.canvas.height) p.speedY *= -1;

                // Draw particle
                this.ctx.beginPath();
                this.ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
                this.ctx.fillStyle = p.color;
                this.ctx.globalAlpha = 0.6;
                this.ctx.fill();
                this.ctx.globalAlpha = 1;

                // Connect particles
                for (let j = i + 1; j < this.particles.length; j++) {
                    const p2 = this.particles[j];
                    const dx = p.x - p2.x;
                    const dy = p.y - p2.y;
                    const dist = Math.sqrt(dx * dx + dy * dy);

                    if (dist < 120) {
                        this.ctx.beginPath();
                        this.ctx.moveTo(p.x, p.y);
                        this.ctx.lineTo(p2.x, p2.y);
                        this.ctx.strokeStyle = p.color;
                        this.ctx.globalAlpha = 0.15 * (1 - dist / 120);
                        this.ctx.stroke();
                        this.ctx.globalAlpha = 1;
                    }
                }

                // Mouse interaction
                if (this.mouse.x && this.mouse.y) {
                    const mdx = p.x - this.mouse.x;
                    const mdy = p.y - this.mouse.y;
                    const mdist = Math.sqrt(mdx * mdx + mdy * mdy);
                    if (mdist < this.mouse.radius) {
                        const force = (this.mouse.radius - mdist) / this.mouse.radius;
                        p.x += mdx * force * 0.02;
                        p.y += mdy * force * 0.02;
                    }
                }
            });

            requestAnimationFrame(() => this.animate());
        }
    }

    // ========================================
    // Typed.js Setup
    // ========================================
    let typedInstance = null;
    let typedSubtitleInstance = null;

    function initTyped() {
        const lang = localStorage.getItem('lang') || 'en';

        // Destroy existing instances to prevent duplication
        if (typedInstance) {
            typedInstance.destroy();
        }
        if (typedSubtitleInstance) {
            typedSubtitleInstance.destroy();
        }

        typedInstance = new Typed('#typed', {
            strings: ['XiaoClaw'],
            typeSpeed: 100,
            startDelay: 500,
            showCursor: false
        });

        const subtitleTexts = {
            en: ['Voice I/O + Local LLM Agent', 'All on a Single ESP32-S3 Chip'],
            zh: ['语音交互 + 本地 LLM Agent', '单芯片 ESP32-S3']
        };

        typedSubtitleInstance = new Typed('#typed-subtitle', {
            strings: subtitleTexts[lang],
            typeSpeed: 50,
            backSpeed: 30,
            startDelay: 1000,
            backDelay: 2000,
            loop: true,
            showCursor: true,
            cursorChar: '|'
        });
    }

    // ========================================
    // AOS Setup
    // ========================================
    function initAOS() {
        AOS.init({
            duration: 800,
            easing: 'ease-out-cubic',
            once: false,
            offset: 50,
            delay: 100,
            disable: window.innerWidth < 768
        });
    }

    // ========================================
    // Language Toggle
    // ========================================
    const langBtns = document.querySelectorAll('.lang-btn');
    let currentLang = localStorage.getItem('lang') || 'zh';

    function setLanguage(lang) {
        currentLang = lang;
        localStorage.setItem('lang', lang);
        document.documentElement.lang = lang;

        langBtns.forEach(btn => {
            btn.classList.toggle('active', btn.dataset.lang === lang);
        });

        // Update all translatable elements
        document.querySelectorAll('[data-i18n]').forEach(el => {
            const key = el.dataset.i18n;
            if (translations[lang] && translations[lang][key]) {
                el.textContent = translations[lang][key];
            }
        });

        // Toggle language-specific elements
        document.querySelectorAll('.en, .zh').forEach(el => {
            el.style.display = '';
        });

        if (lang === 'zh') {
            document.querySelectorAll('.en').forEach(el => el.style.display = 'none');
        } else {
            document.querySelectorAll('.zh').forEach(el => el.style.display = 'none');
        }

        // Reinitialize Typed.js with new language
        document.getElementById('typed').innerHTML = '';
        document.getElementById('typed-subtitle').innerHTML = '';
        initTyped();
    }

    langBtns.forEach(btn => {
        btn.addEventListener('click', () => setLanguage(btn.dataset.lang));
    });

    // ========================================
    // Navbar Scroll Effect
    // ========================================
    const nav = document.querySelector('.nav');
    window.addEventListener('scroll', () => {
        if (window.scrollY > 50) {
            nav.style.background = 'rgba(10, 10, 15, 0.95)';
            nav.style.boxShadow = '0 4px 20px rgba(0, 0, 0, 0.3)';
        } else {
            nav.style.background = 'rgba(10, 10, 15, 0.8)';
            nav.style.boxShadow = 'none';
        }
    });

    // ========================================
    // Smooth Scroll
    // ========================================
    document.querySelectorAll('a[href^="#"]').forEach(anchor => {
        anchor.addEventListener('click', function(e) {
            e.preventDefault();
            const target = document.querySelector(this.getAttribute('href'));
            if (target) {
                target.scrollIntoView({
                    behavior: 'smooth',
                    block: 'start'
                });
            }
        });
    });

    // ========================================
    // Translations
    // ========================================
    const translations = {
        en: {
            'nav.features': 'Features',
            'nav.architecture': 'Architecture',
            'nav.hardware': 'Hardware',
            'nav.quickstart': 'Quick Start',
            'hero.badge': 'ESP32-S3 AI Voice Assistant',
            'hero.get_started': 'Get Started',
            'hero.view_github': 'View on GitHub',
            'hero.flash': 'Flash',
            'hero.psram': 'PSRAM',
            'hero.boards': 'Supported Boards',
            'hero.scroll': 'Scroll to explore',
            'hero.desc': 'Local AI Agent firmware running on ESP32-S3, integrating offline voice wake-up with cloud TTS, supporting local LLM inference, tool calling, long-term memory storage and autonomous task execution.',
            'features.title': 'Features',
            'features.desc': 'Two powerful layers working in harmony',
            'features.voice_title': 'Voice I/O Layer',
            'features.voice_1': 'Offline wake word detection (ESP-SR)',
            'features.voice_2': 'Streaming ASR + TTS via server',
            'features.voice_3': 'OPUS audio codec',
            'features.voice_4': 'OLED/LCD display with emoji',
            'features.voice_5': 'Battery & power management',
            'features.voice_6': 'Multi-language support',
            'features.agent_title': 'Agent Brain Layer',
            'features.agent_1': 'LLM API (Claude / GPT)',
            'features.agent_2': 'ReAct tool calling',
            'features.agent_3': 'Long-term memory (SPIFFS)',
            'features.agent_4': 'Session consolidation',
            'features.agent_5': 'Cron scheduler',
            'features.agent_6': 'Web search capability',
            'arch.title': 'Architecture',
            'arch.desc': 'How the components work together',
            'arch.voice_label': 'Voice I/O (xiaozhi)',
            'arch.bridge_label': 'Bridge Layer',
            'arch.agent_label': 'Agent Brain (mimiclaw)',
            'hardware.title': 'Supported Hardware',
            'hardware.desc': 'Compatible with 70+ ESP32-S3 boards',
            'hardware.more': 'And 70+ more...',
            'quickstart.title': 'Quick Start',
            'quickstart.desc': 'Get up and running in 3 steps',
            'quickstart.step1_title': 'Clone & Configure',
            'quickstart.step1_desc': 'Clone the repository and set your target ESP32-S3 board',
            'quickstart.step2_title': 'Configure Secrets',
            'quickstart.step2_desc': 'Set up WiFi, API keys via menuconfig under Xiaozhi Assistant → Secret Configuration',
            'quickstart.step3_title': 'Build & Flash',
            'quickstart.step3_desc': 'Build the firmware and flash to your device',
            'links.github_desc': 'Source code & issues',
            'links.docs_title': 'Documentation',
            'links.docs_desc': 'Full API reference',
            'links.xiaozhi_desc': 'Voice interaction framework',
            'links.mimiclaw_desc': 'ESP32 AI agent',
            'built.title': 'Built On',
            'built.desc': 'XiaoClaw combines the best of both worlds',
            'built.xiaozhi_desc': 'Voice interaction — audio, playback, wake word, display, network',
            'built.mimiclaw_desc': 'ESP32 AI Agent — LLM reasoning, tool calling, memory, autonomous tasks',
            'footer.copyright': 'MIT License. Built with ESP-IDF and Claude Code',
            'footer.github': 'GitHub',
            'footer.docs': 'Docs'
        },
        zh: {
            'nav.features': '功能特性',
            'nav.architecture': '系统架构',
            'nav.hardware': '支持硬件',
            'nav.quickstart': '快速开始',
            'hero.badge': 'ESP32-S3 AI 语音助手',
            'hero.get_started': '立即开始',
            'hero.view_github': '在 GitHub 查看',
            'hero.flash': '闪存',
            'hero.psram': '内存',
            'hero.boards': '支持板型',
            'hero.scroll': '向下滚动',
            'hero.desc': '运行于 ESP32-S3 的本地 AI Agent 固件，集成离线语音唤醒与云端 TTS 服务，支持本地大模型推理、工具调用、长期记忆存储与自主任务执行。',
            'features.title': '功能特性',
            'features.desc': '两大核心层，协同工作',
            'features.voice_title': '语音 I/O 层',
            'features.voice_1': '离线唤醒词检测 (ESP-SR)',
            'features.voice_2': '流式 ASR + TTS',
            'features.voice_3': 'OPUS 音频编解码',
            'features.voice_4': 'OLED/LCD 显示屏支持 emoji',
            'features.voice_5': '电池与电源管理',
            'features.voice_6': '多语言支持',
            'features.agent_title': 'Agent 大脑层',
            'features.agent_1': 'LLM API (Claude / GPT)',
            'features.agent_2': 'ReAct 工具调用',
            'features.agent_3': '长期记忆 (SPIFFS)',
            'features.agent_4': '会话整合',
            'features.agent_5': '定时任务调度',
            'features.agent_6': '网页搜索能力',
            'arch.title': '系统架构',
            'arch.desc': '组件如何协同工作',
            'arch.voice_label': '语音 I/O (xiaozhi)',
            'arch.bridge_label': '桥接层',
            'arch.agent_label': 'Agent 大脑 (mimiclaw)',
            'hardware.title': '支持硬件',
            'hardware.desc': '兼容 70+ ESP32-S3 开发板',
            'hardware.more': '更多...',
            'quickstart.title': '快速开始',
            'quickstart.desc': '3 步快速上手',
            'quickstart.step1_title': '克隆与配置',
            'quickstart.step1_desc': '克隆仓库并设置目标 ESP32-S3 板型',
            'quickstart.step2_title': '配置密钥',
            'quickstart.step2_desc': '在 Xiaozhi Assistant → Secret Configuration 下配置 WiFi 和 API 密钥',
            'quickstart.step3_title': '编译与烧录',
            'quickstart.step3_desc': '编译固件并烧录到设备',
            'links.github_desc': '源代码和问题',
            'links.docs_title': '文档',
            'links.docs_desc': '完整 API 参考',
            'links.xiaozhi_desc': '语音交互框架',
            'links.mimiclaw_desc': 'ESP32 AI Agent',
            'built.title': '基于以下优秀项目构建',
            'built.desc': 'XiaoClaw 结合了两者的优点',
            'built.xiaozhi_desc': '语音交互 — 音频采集、回放、唤醒词、显示屏、网络通信',
            'built.mimiclaw_desc': 'ESP32 AI Agent — LLM 推理、工具调用、记忆管理、自主任务执行',
            'footer.copyright': 'MIT 协议. 基于 ESP-IDF 和 Claude Code 构建',
            'footer.github': 'GitHub',
            'footer.docs': '文档'
        }
    };

    // ========================================
    // Initialize
    // ========================================
    document.addEventListener('DOMContentLoaded', () => {
        // Set initial language
        setLanguage(currentLang);

        // Initialize particles
        new Particles();

        // Initialize AOS
        initAOS();

        // Initialize Typed.js
        initTyped();
    });

})();
