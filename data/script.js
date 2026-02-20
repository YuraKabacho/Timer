class SplitFlopController {
  constructor() {
    this.config = {
      startDate: "2026-01-01",
      startTime: "12:00:00",
      durationValue: 0,
      durationUnit: "days",
      syncHour: 3,
      autoSync: true,
      timerStopped: true,
      useCurrentOnStart: false,
      calibrateOnStart: false,
    };

    this.testDigits = [0, 0, 0, 0];
    this.hourDigits = [0, 0];
    this.fullUpdateTimer = null;

    this.currentTime = null;
    this.endDateString = "Обчислюється...";
    this.startMomentStatic = null;
    this.timeoutWarningShown = false; // прапорець, щоб не показувати сповіщення двічі

    this.calibrateOnStart = false;
    this.ws = null;
    this.calibrationInProgress = false;
    this.motorsHomed = true; // чи відкалібровані двигуни
    this.savedTestDigits = [...this.testDigits];

    // Для тултіпа
    this.tooltip = null;
    this.tooltipVisible = false;
    this.tooltipText = "";

    this.init();
  }

  async init() {
    this.createTooltipElement();
    this.initEventListeners();
    this.initTestDigits();
    this.initHourDigits();
    this.initUnitSelector();
    this.initAutoSyncToggle();
    this.initWebSocket();
    this.startStatusUpdates();
    await this.loadConfig();
    this.calculateEndDate();
    this.updateStartMomentDisplay();
    this.updateStartStopButton();
    this.updateCalibrateOnStartButton();

    document.getElementById("last-update").textContent =
      new Date().toLocaleDateString("uk-UA");

    setInterval(() => {
      console.log("Auto‑save (5 min)");
      this.saveConfig(false);
    }, 300000);
  }

  // Створюємо елемент для кастомного тултіпа
  createTooltipElement() {
    this.tooltip = document.createElement("div");
    this.tooltip.id = "custom-tooltip";
    this.tooltip.style.position = "fixed";
    this.tooltip.style.display = "none";
    this.tooltip.style.backgroundColor = "rgba(0,0,0,0.8)";
    this.tooltip.style.color = "#fff";
    this.tooltip.style.padding = "8px 12px";
    this.tooltip.style.borderRadius = "4px";
    this.tooltip.style.fontSize = "14px";
    this.tooltip.style.zIndex = "10000";
    this.tooltip.style.pointerEvents = "none";
    this.tooltip.style.whiteSpace = "nowrap";
    this.tooltip.style.boxShadow = "0 2px 10px rgba(0,0,0,0.2)";
    document.body.appendChild(this.tooltip);

    // Відстеження руху миші для позиціонування
    document.addEventListener("mousemove", (e) => {
      if (this.tooltipVisible) {
        this.tooltip.style.left = e.clientX + 15 + "px";
        this.tooltip.style.top = e.clientY + 15 + "px";
      }
    });
  }

  showTooltip(text, x, y) {
    this.tooltipText = text;
    this.tooltip.textContent = text;
    this.tooltip.style.display = "block";
    this.tooltip.style.left = x + 15 + "px";
    this.tooltip.style.top = y + 15 + "px";
    this.tooltipVisible = true;
  }

  hideTooltip() {
    this.tooltip.style.display = "none";
    this.tooltipVisible = false;
  }

  // ---------- WebSocket ----------
  initWebSocket() {
    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const wsUrl = `${protocol}//${window.location.host}/ws`;
    this.ws = new WebSocket(wsUrl);

    this.ws.onopen = () => {
      console.log("WebSocket connected");
    };

    this.ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        this.handleWebSocketData(data);
      } catch (e) {
        console.error("Invalid WebSocket message", e);
      }
    };

    this.ws.onclose = () => {
      console.log("WebSocket disconnected, will reconnect...");
      setTimeout(() => this.initWebSocket(), 3000);
    };
  }

  handleWebSocketData(data) {
    // Оновлюємо стан калібрування та моторів
    if (data.calibrationInProgress !== undefined) {
      this.calibrationInProgress = data.calibrationInProgress;
      this.updateUIBlockedState();
    }
    if (data.motorsHomed !== undefined) {
      this.motorsHomed = data.motorsHomed;
      this.updateUIBlockedState();
    }

    if (data.currentTimeFormatted) {
      document.getElementById("current-time").textContent =
        data.currentTimeFormatted;
      const [hours, minutes, seconds] = data.currentTimeFormatted
        .split(":")
        .map(Number);
      const now = new Date();
      now.setHours(hours, minutes, seconds, 0);
      this.currentTime = now;
    }

    if (data.timeRemaining) {
      document.getElementById("time-remaining").textContent =
        data.timeRemaining;
    }
    if (data.remainingSeconds !== undefined) {
      const detailed = this.formatDetailedRemaining(data.remainingSeconds);
      document.getElementById("time-remaining-detailed").textContent = detailed;
    }

    if (
      data.motorsHomed !== undefined ||
      data.calibrationInProgress !== undefined
    ) {
      this.updateCalibrationBadge(data.motorsHomed, data.calibrationInProgress);
    }

    if (data.segmentValues) {
      const digits = document.querySelectorAll(".digit-sim");
      digits.forEach((digit, index) => {
        if (data.segmentValues[index] !== undefined) {
          const newValue = data.segmentValues[index];
          if (digit.textContent !== newValue.toString()) {
            digit.textContent = newValue;
            this.animateFlip(digit);
          }
        }
      });
    }

    if (
      data.durationValue !== undefined &&
      data.durationValue !== this.config.durationValue
    ) {
      this.config.durationValue = data.durationValue;
      const str = data.durationValue.toString().padStart(4, "0");
      for (let i = 0; i < 4; i++) {
        this.testDigits[i] = parseInt(str[i]) || 0;
        const digitEl = document.querySelector(
          `.test-digit[data-segment="${i}"] .digit-number`,
        );
        if (digitEl) digitEl.textContent = this.testDigits[i];
      }
      this.savedTestDigits = [...this.testDigits];
    }

    if (data.timerStopped !== undefined) {
      this.config.timerStopped = data.timerStopped;
      this.updateStartStopButton();
      if (this.config.timerStopped) {
        this.checkPastEndDate();
      } else {
        this.hidePersistentNotification();
      }
    }

    if (data.startTimestamp && !this.config.timerStopped) {
      const serverStart = new Date(data.startTimestamp * 1000);
      if (
        !this.startMomentStatic ||
        this.startMomentStatic.getTime() !== serverStart.getTime()
      ) {
        this.startMomentStatic = serverStart;
      }
    }

    if (data.startDate && data.startTime && !this.config.useCurrentOnStart) {
      const dateInput = document.getElementById("start-date");
      const timeInput = document.getElementById("start-time");
      if (dateInput.value !== data.startDate) {
        dateInput.value = data.startDate;
      }
      if (timeInput.value !== data.startTime) {
        timeInput.value = data.startTime;
      }
      this.config.startDate = data.startDate;
      this.config.startTime = data.startTime;
      this.calculateEndDate();
    }

    if (data.durationUnit) {
      this.config.durationUnit = data.durationUnit;
      this.setActiveUnit(data.durationUnit);
    }
    if (data.syncHour !== undefined) {
      this.config.syncHour = data.syncHour;
      this.hourDigits = [Math.floor(data.syncHour / 10), data.syncHour % 10];
      this.updateHourDisplay();
    }
    if (data.autoSync !== undefined) {
      this.config.autoSync = data.autoSync;
      this.updateAutoSyncButton();
    }
    if (data.useCurrentOnStart !== undefined) {
      this.config.useCurrentOnStart = data.useCurrentOnStart;
      document.getElementById("use-current-on-start").checked =
        data.useCurrentOnStart;
      this.toggleStartFields(data.useCurrentOnStart);
    }
    if (data.calibrateOnStart !== undefined) {
      this.config.calibrateOnStart = data.calibrateOnStart;
      this.calibrateOnStart = data.calibrateOnStart;
      this.updateCalibrateOnStartButton();
    }

    this.updateStartMomentDisplay();
  }

  // ---------- Блокування UI залежно від стану калібрування ----------
  updateUIBlockedState() {
    const blocked = this.calibrationInProgress || !this.motorsHomed;
    const startBtn = document.getElementById("btn-start-stop");
    const digitControls = document.querySelectorAll(
      ".test-digit .digit-btn, .test-digit .digit-number",
    );
    const unitCards = document.querySelectorAll(".unit-card");
    const hourControls = document.querySelectorAll(
      "#hour-digits .digit-btn, #hour-digits .digit-number",
    );

    const tooltipMessage = this.calibrationInProgress
      ? "⏳ Триває калібрування, зачекайте"
      : "⚠️ Таймер не відкалібрований";

    if (blocked) {
      // Блокуємо кнопку старт
      if (startBtn) {
        startBtn.disabled = true;
        startBtn.style.opacity = "0.5";
        startBtn.style.pointerEvents = "none";
        this.attachTooltip(startBtn, tooltipMessage);
      }

      // Блокуємо елементи керування цифрами
      digitControls.forEach((el) => {
        el.style.pointerEvents = "none";
        el.style.opacity = "0.5";
        this.attachTooltip(el, tooltipMessage);
      });

      // Блокуємо вибір одиниць
      unitCards.forEach((card) => {
        card.style.pointerEvents = "none";
        card.style.opacity = "0.5";
        this.attachTooltip(card, tooltipMessage);
      });

      // Блокуємо години синхронізації
      hourControls.forEach((el) => {
        el.style.pointerEvents = "none";
        el.style.opacity = "0.5";
        this.attachTooltip(el, tooltipMessage);
      });
    } else {
      // Розблоковуємо
      if (startBtn) {
        startBtn.disabled = false;
        startBtn.style.opacity = "1";
        startBtn.style.pointerEvents = "auto";
        this.removeTooltip(startBtn);
      }

      digitControls.forEach((el) => {
        el.style.pointerEvents = "auto";
        el.style.opacity = "1";
        this.removeTooltip(el);
      });

      unitCards.forEach((card) => {
        card.style.pointerEvents = "auto";
        card.style.opacity = "1";
        this.removeTooltip(card);
      });

      hourControls.forEach((el) => {
        el.style.pointerEvents = "auto";
        el.style.opacity = "1";
        this.removeTooltip(el);
      });
    }
  }

  attachTooltip(element, message) {
    // Видаляємо попередній listener, щоб не накопичувати
    element.removeEventListener("mouseenter", element._tooltipEnter);
    element.removeEventListener("mouseleave", element._tooltipLeave);

    const enterHandler = (e) => {
      this.showTooltip(message, e.clientX, e.clientY);
    };
    const leaveHandler = () => {
      this.hideTooltip();
    };

    element.addEventListener("mouseenter", enterHandler);
    element.addEventListener("mouseleave", leaveHandler);

    // Зберігаємо посилання для можливого видалення
    element._tooltipEnter = enterHandler;
    element._tooltipLeave = leaveHandler;
  }

  removeTooltip(element) {
    element.removeEventListener("mouseenter", element._tooltipEnter);
    element.removeEventListener("mouseleave", element._tooltipLeave);
    delete element._tooltipEnter;
    delete element._tooltipLeave;
  }

  // ---------- Config loading / saving ----------
  async loadConfig() {
    try {
      const response = await fetch("/api/config");
      if (!response.ok) throw new Error("Не вдалося отримати конфіг");
      const data = await response.json();

      if (data.startDate) {
        this.config.startDate = data.startDate;
        document.getElementById("start-date").value = data.startDate;
      }
      if (data.startTime) {
        this.config.startTime = data.startTime;
        document.getElementById("start-time").value = data.startTime;
      }

      if (data.durationValue !== undefined) {
        this.config.durationValue = data.durationValue;
        const str = data.durationValue.toString().padStart(4, "0");
        for (let i = 0; i < 4; i++) {
          this.testDigits[i] = parseInt(str[i]) || 0;
          const digitEl = document.querySelector(
            `.test-digit[data-segment="${i}"] .digit-number`,
          );
          if (digitEl) digitEl.textContent = this.testDigits[i];
        }
        this.savedTestDigits = [...this.testDigits];
      }

      if (data.durationUnit) {
        this.config.durationUnit = data.durationUnit;
        this.setActiveUnit(data.durationUnit);
      }

      if (data.syncHour !== undefined) {
        this.config.syncHour = data.syncHour;
        this.hourDigits = [Math.floor(data.syncHour / 10), data.syncHour % 10];
        this.updateHourDisplay();
      }

      if (data.autoSync !== undefined) {
        this.config.autoSync = data.autoSync;
        this.updateAutoSyncButton();
      }

      if (data.useCurrentOnStart !== undefined) {
        this.config.useCurrentOnStart = data.useCurrentOnStart;
        const checkbox = document.getElementById("use-current-on-start");
        checkbox.checked = data.useCurrentOnStart;
        this.toggleStartFields(data.useCurrentOnStart);
      }

      if (data.calibrateOnStart !== undefined) {
        this.config.calibrateOnStart = data.calibrateOnStart;
        this.calibrateOnStart = data.calibrateOnStart;
        this.updateCalibrateOnStartButton();
      }

      if (data.startTimestamp) {
        this.startMomentStatic = new Date(data.startTimestamp * 1000);
      }

      this.calculateEndDate();
      this.updateStartMomentDisplay();
      this.checkPastEndDate();
    } catch (error) {
      console.error("Помилка завантаження конфігурації:", error);
    }
  }

  async saveConfig(showToast = false) {
    const durationStr = this.testDigits.join("");
    const durationValue = parseInt(durationStr, 10) || 0;
    this.config.durationValue = durationValue;

    const payload = {
      durationValue: durationValue,
      durationUnit: this.config.durationUnit,
      syncHour: this.config.syncHour,
      autoSync: this.config.autoSync,
      useCurrentOnStart: this.config.useCurrentOnStart,
      calibrateOnStart: this.calibrateOnStart,
    };

    if (!this.config.useCurrentOnStart) {
      payload.startDate = document.getElementById("start-date").value;
      payload.startTime = document.getElementById("start-time").value;
    }

    try {
      const response = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });

      if (response.ok) {
        if (showToast) {
          this.showToast("Налаштування збережено успішно", "success");
        }
        await this.loadConfig();
      } else {
        const error = await response
          .json()
          .catch(() => ({ error: "Unknown error" }));
        throw new Error(error.error || "Помилка збереження");
      }
    } catch (error) {
      console.error("Save error:", error);
      if (showToast) {
        this.showToast(
          error.message || "Помилка збереження налаштувань",
          "error",
        );
      }
    }
  }

  toggleStartFields(useCurrent) {
    const fields = document.getElementById("start-datetime-fields");
    const momentBlock = document.getElementById("current-start-moment");
    if (useCurrent) {
      fields.style.display = "none";
      momentBlock.style.display = "block";
    } else {
      fields.style.display = "block";
      momentBlock.style.display = "none";
    }
  }

  updateStartMomentDisplay() {
    const displayEl = document.getElementById("start-moment-display");
    if (!displayEl) return;

    if (!this.config.timerStopped && this.startMomentStatic) {
      displayEl.textContent = this.formatDateTime(this.startMomentStatic);
      return;
    }

    if (this.config.useCurrentOnStart) {
      if (this.currentTime) {
        displayEl.textContent = this.formatDateTime(this.currentTime);
      } else {
        displayEl.textContent = "--:--:--";
      }
    } else {
      displayEl.textContent = `${this.config.startDate} ${this.config.startTime}`;
    }
  }

  formatDateTime(date) {
    if (!date) return "--:--:--";
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, "0");
    const day = String(date.getDate()).padStart(2, "0");
    const hours = String(date.getHours()).padStart(2, "0");
    const minutes = String(date.getMinutes()).padStart(2, "0");
    const seconds = String(date.getSeconds()).padStart(2, "0");
    return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
  }

  scheduleFullUpdate() {
    if (this.fullUpdateTimer) {
      clearTimeout(this.fullUpdateTimer);
    }
    this.fullUpdateTimer = setTimeout(() => {
      this.sendFullValue();
    }, 300);
  }

  async sendFullValue() {
    if (this.calibrationInProgress || !this.motorsHomed) {
      console.log(
        "Calibration in progress or motors not homed – not sending value",
      );
      return;
    }
    const fullValue = parseInt(this.testDigits.join(""), 10) || 0;
    try {
      await fetch("/api/testall", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: `value=${fullValue}`,
      });
    } catch (err) {
      console.error("Failed to set full value:", err);
    }
  }

  initTestDigits() {
    const testDigits = document.querySelectorAll(".test-digit");

    testDigits.forEach((digitElement, index) => {
      const digitNumber = digitElement.querySelector(".digit-number");
      const upBtn = digitElement.querySelector(".digit-btn.up");
      const downBtn = digitElement.querySelector(".digit-btn.down");

      const changeDigit = (increment) => {
        if (this.calibrationInProgress || !this.motorsHomed) {
          const msg = this.calibrationInProgress
            ? "Калібрування триває – зміну цифр заборонено"
            : "Таймер не відкалібрований – зміну цифр заборонено";
          this.showToast(msg, "warning");
          digitNumber.textContent = this.testDigits[index];
          return;
        }
        let newValue = (this.testDigits[index] + increment + 10) % 10;
        this.testDigits[index] = newValue;
        digitNumber.textContent = newValue;
        this.animateFlip(digitNumber);
        this.calculateEndDate();
        this.scheduleFullUpdate();
        this.saveConfig(false);
      };

      digitNumber.addEventListener("click", () => {
        if (this.calibrationInProgress || !this.motorsHomed) {
          const msg = this.calibrationInProgress
            ? "Калібрування триває – зміну цифр заборонено"
            : "Таймер не відкалібрований – зміну цифр заборонено";
          this.showToast(msg, "warning");
          return;
        }
        let newValue = parseInt(
          prompt(
            `Введіть цифру для сегмента ${index + 1} (0-9):`,
            this.testDigits[index],
          ),
          10,
        );
        if (!isNaN(newValue) && newValue >= 0 && newValue <= 9) {
          this.testDigits[index] = newValue;
          digitNumber.textContent = newValue;
          this.animateFlip(digitNumber);
          this.calculateEndDate();
          this.scheduleFullUpdate();
          this.saveConfig(false);
        }
      });

      upBtn.addEventListener("click", () => changeDigit(1));
      downBtn.addEventListener("click", () => changeDigit(-1));
    });
  }

  initHourDigits() {
    const tenUp = document.getElementById("hour-ten-up");
    const tenDown = document.getElementById("hour-ten-down");
    const tenDigit = document.getElementById("hour-ten-digit");
    const oneUp = document.getElementById("hour-one-up");
    const oneDown = document.getElementById("hour-one-down");
    const oneDigit = document.getElementById("hour-one-digit");

    if (!tenUp || !tenDown || !tenDigit || !oneUp || !oneDown || !oneDigit) {
      console.warn("Hour digit elements not found");
      return;
    }

    this.hourDigits = [0, 0];
    this.updateHourDisplay();

    const changeHour = (position, increment) => {
      if (this.calibrationInProgress || !this.motorsHomed) {
        const msg = this.calibrationInProgress
          ? "Калібрування триває – зміну години заборонено"
          : "Таймер не відкалібрований – зміну години заборонено";
        this.showToast(msg, "warning");
        return;
      }
      if (position === 0) {
        this.hourDigits[0] = (this.hourDigits[0] + increment + 3) % 3;
        if (this.hourDigits[0] === 2 && this.hourDigits[1] > 3) {
          this.hourDigits[1] = 3;
        }
      } else {
        let max = this.hourDigits[0] === 2 ? 4 : 10;
        this.hourDigits[1] = (this.hourDigits[1] + increment + max) % max;
      }
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
      this.saveConfig(false);
    };

    tenUp.addEventListener("click", () => changeHour(0, 1));
    tenDown.addEventListener("click", () => changeHour(0, -1));
    oneUp.addEventListener("click", () => changeHour(1, 1));
    oneDown.addEventListener("click", () => changeHour(1, -1));

    tenDigit.addEventListener("click", () => this.promptHourDigit(0));
    oneDigit.addEventListener("click", () => this.promptHourDigit(1));
  }

  promptHourDigit(position) {
    if (this.calibrationInProgress || !this.motorsHomed) {
      const msg = this.calibrationInProgress
        ? "Калібрування триває – зміну години заборонено"
        : "Таймер не відкалібрований – зміну години заборонено";
      this.showToast(msg, "warning");
      return;
    }
    let max = position === 0 ? 2 : this.hourDigits[0] === 2 ? 3 : 9;
    let newVal = parseInt(
      prompt(
        `Введіть цифру для ${position === 0 ? "десятків" : "одиниць"} години (0-${max}):`,
        this.hourDigits[position],
      ),
      10,
    );
    if (!isNaN(newVal) && newVal >= 0 && newVal <= max) {
      this.hourDigits[position] = newVal;
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
      this.saveConfig(false);
    }
  }

  updateHourDisplay() {
    const tenDigit = document.getElementById("hour-ten-digit");
    const oneDigit = document.getElementById("hour-one-digit");
    if (tenDigit) tenDigit.textContent = this.hourDigits[0];
    if (oneDigit) oneDigit.textContent = this.hourDigits[1];
  }

  initAutoSyncToggle() {
    const btn = document.getElementById("btn-auto-sync");
    if (!btn) return;
    btn.addEventListener("click", () => {
      this.config.autoSync = !this.config.autoSync;
      this.updateAutoSyncButton();
      this.saveConfig(false);
    });
  }

  updateAutoSyncButton() {
    const btn = document.getElementById("btn-auto-sync");
    if (!btn) return;
    if (this.config.autoSync) {
      btn.innerHTML = '<i class="fas fa-sync-alt"></i> Автосинхронізація: Вкл';
      btn.classList.add("active");
    } else {
      btn.innerHTML = '<i class="fas fa-sync-alt"></i> Автосинхронізація: Викл';
      btn.classList.remove("active");
    }
  }

  initUnitSelector() {
    const cards = document.querySelectorAll(".unit-card");
    cards.forEach((card) => {
      card.addEventListener("click", async () => {
        if (this.calibrationInProgress || !this.motorsHomed) {
          const msg = this.calibrationInProgress
            ? "Калібрування триває – зміну одиниці заборонено"
            : "Таймер не відкалібрований – зміну одиниці заборонено";
          this.showToast(msg, "warning");
          return;
        }
        const unit = card.dataset.unit;
        this.config.durationUnit = unit;
        this.setActiveUnit(unit);
        this.calculateEndDate();

        if (!this.config.timerStopped) {
          await this.toggleTimer();
        }
        this.saveConfig(false);
      });
    });
  }

  setActiveUnit(unit) {
    const cards = document.querySelectorAll(".unit-card");
    cards.forEach((card) => {
      if (card.dataset.unit === unit) {
        card.classList.add("active");
      } else {
        card.classList.remove("active");
      }
    });
  }

  animateFlip(element) {
    element.style.animation = "flip 0.5s ease";
    setTimeout(() => (element.style.animation = ""), 500);
  }

  calculateEndDate() {
    let startDateTime;

    if (this.config.useCurrentOnStart && this.config.timerStopped) {
      startDateTime = this.currentTime
        ? new Date(this.currentTime)
        : new Date();
    } else if (
      this.config.useCurrentOnStart &&
      !this.config.timerStopped &&
      this.startMomentStatic
    ) {
      startDateTime = new Date(this.startMomentStatic);
    } else {
      const startDate = document.getElementById("start-date").value;
      const startTime = document.getElementById("start-time").value;
      startDateTime = new Date(`${startDate}T${startTime}`);
    }

    if (isNaN(startDateTime.getTime())) {
      this.endDateString = "Невірна дата";
      document.getElementById("end-date-display").textContent =
        this.endDateString;
      return;
    }

    const durationStr = this.testDigits.join("");
    const durationValue = parseInt(durationStr, 10) || 0;
    this.config.durationValue = durationValue;

    let endDateTime = new Date(startDateTime);
    const unit = this.config.durationUnit;

    switch (unit) {
      case "days":
        endDateTime.setDate(endDateTime.getDate() + durationValue);
        break;
      case "hours":
        endDateTime.setHours(endDateTime.getHours() + durationValue);
        break;
      case "minutes":
        endDateTime.setMinutes(endDateTime.getMinutes() + durationValue);
        break;
      case "seconds":
        endDateTime.setSeconds(endDateTime.getSeconds() + durationValue);
        break;
    }

    this.endDateString = endDateTime.toLocaleDateString("uk-UA", {
      year: "numeric",
      month: "long",
      day: "numeric",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });

    document.getElementById("end-date-display").textContent =
      this.endDateString;
    this.checkPastEndDate();
  }

  checkPastEndDate() {
    if (!this.config.timerStopped) return;

    const endDate = this.getEndDateObject();
    if (!endDate) return;

    const now = new Date();
    if (endDate < now) {
      this.showPersistentNotification(
        "warning",
        "⚠️ Дата завершення вже минула!\n\nРекомендації:\n• Увімкніть 'Запустити відлік від поточного моменту' та натисніть 'Старт';\n• Або змініть дату/час старту або тривалість.",
      );
    } else {
      this.hidePersistentNotification();
    }
  }

  getEndDateObject() {
    let startDateTime;

    if (this.config.useCurrentOnStart && this.config.timerStopped) {
      startDateTime = this.currentTime
        ? new Date(this.currentTime)
        : new Date();
    } else if (
      this.config.useCurrentOnStart &&
      !this.config.timerStopped &&
      this.startMomentStatic
    ) {
      startDateTime = new Date(this.startMomentStatic);
    } else {
      const startDate = document.getElementById("start-date").value;
      const startTime = document.getElementById("start-time").value;
      startDateTime = new Date(`${startDate}T${startTime}`);
    }

    if (isNaN(startDateTime.getTime())) return null;

    const durationValue = this.config.durationValue;
    const endDateTime = new Date(startDateTime);
    switch (this.config.durationUnit) {
      case "days":
        endDateTime.setDate(endDateTime.getDate() + durationValue);
        break;
      case "hours":
        endDateTime.setHours(endDateTime.getHours() + durationValue);
        break;
      case "minutes":
        endDateTime.setMinutes(endDateTime.getMinutes() + durationValue);
        break;
      case "seconds":
        endDateTime.setSeconds(endDateTime.getSeconds() + durationValue);
        break;
    }
    return endDateTime;
  }

  showPersistentNotification(type, message) {
    const notif = document.getElementById("persistent-notification");
    const msgDiv = notif.querySelector(".notification-message");
    const closeBtn = notif.querySelector(".close-btn");

    notif.className = `persistent-notification ${type}`;
    msgDiv.innerHTML = message.replace(/\n/g, "<br>");
    notif.classList.remove("hidden");

    closeBtn.onclick = () => {
      notif.classList.add("hidden");
    };
  }

  hidePersistentNotification() {
    const notif = document.getElementById("persistent-notification");
    notif.classList.add("hidden");
  }

  formatDetailedRemaining(seconds) {
    if (seconds <= 0) return "0 секунд";

    const SECONDS_IN_MINUTE = 60;
    const SECONDS_IN_HOUR = 3600;
    const SECONDS_IN_DAY = 86400;
    const SECONDS_IN_MONTH = 2592000;
    const SECONDS_IN_YEAR = 31536000;

    let remaining = seconds;
    let years = Math.floor(remaining / SECONDS_IN_YEAR);
    remaining %= SECONDS_IN_YEAR;
    let months = Math.floor(remaining / SECONDS_IN_MONTH);
    remaining %= SECONDS_IN_MONTH;
    let days = Math.floor(remaining / SECONDS_IN_DAY);
    remaining %= SECONDS_IN_DAY;
    let hours = Math.floor(remaining / SECONDS_IN_HOUR);
    remaining %= SECONDS_IN_HOUR;
    let minutes = Math.floor(remaining / SECONDS_IN_MINUTE);
    let secs = remaining % SECONDS_IN_MINUTE;

    const parts = [];
    if (years > 0) parts.push(`${years} р.`);
    if (months > 0) parts.push(`${months} міс.`);
    if (days > 0) parts.push(`${days} дн.`);
    if (hours > 0) parts.push(`${hours} год.`);
    if (minutes > 0) parts.push(`${minutes} хв.`);
    if (secs > 0 || parts.length === 0) parts.push(`${secs} сек.`);

    return parts.join(" ");
  }

  initEventListeners() {
    document.getElementById("start-date").addEventListener("change", () => {
      if (!this.config.useCurrentOnStart) this.calculateEndDate();
    });
    document.getElementById("start-time").addEventListener("change", () => {
      if (!this.config.useCurrentOnStart) this.calculateEndDate();
    });

    document
      .getElementById("use-current-on-start")
      .addEventListener("change", (e) => {
        this.config.useCurrentOnStart = e.target.checked;
        this.toggleStartFields(e.target.checked);
        this.calculateEndDate();
        this.updateStartMomentDisplay();
        this.saveConfig(false);
      });

    document
      .getElementById("btn-save")
      .addEventListener("click", () => this.saveConfig(true));
    document
      .getElementById("btn-sync")
      .addEventListener("click", () => this.syncTime());
    document
      .getElementById("btn-calibrate")
      .addEventListener("click", () => this.calibrateMotors());
    document
      .getElementById("btn-start-stop")
      .addEventListener("click", () => this.toggleTimer());
    document
      .getElementById("btn-reset")
      .addEventListener("click", () => this.resetDigits());

    const calibrateOnStartBtn = document.getElementById(
      "btn-calibrate-on-start",
    );
    calibrateOnStartBtn.addEventListener("click", () => {
      this.calibrateOnStart = !this.calibrateOnStart;
      this.updateCalibrateOnStartButton();
      this.saveConfig(false);
    });
  }

  async resetDigits() {
    if (this.calibrationInProgress || !this.motorsHomed) {
      const msg = this.calibrationInProgress
        ? "Калібрування триває – скидання заборонено"
        : "Таймер не відкалібрований – скидання заборонено";
      this.showToast(msg, "warning");
      return;
    }
    this.testDigits = [0, 0, 0, 0];
    this.updateTestDigitsUI();
    this.calculateEndDate();
    try {
      const response = await fetch("/api/reset", { method: "POST" });
      if (response.ok) {
        await this.loadConfig();
        this.showToast("Цифри скинуто до 0", "success");
      } else {
        this.showToast("Помилка скидання", "error");
      }
    } catch (err) {
      this.showToast("Помилка скидання", "error");
    }
  }

  updateStartStopButton() {
    const btn = document.getElementById("btn-start-stop");
    if (!btn) return;
    if (this.config.timerStopped) {
      btn.innerHTML = '<i class="fas fa-play"></i> Старт';
      btn.classList.remove("btn-danger");
      btn.classList.add("btn-success");
    } else {
      btn.innerHTML = '<i class="fas fa-pause"></i> Стоп';
      btn.classList.remove("btn-success");
      btn.classList.add("btn-danger");
    }
  }

  updateCalibrateOnStartButton() {
    const btn = document.getElementById("btn-calibrate-on-start");
    if (!btn) return;
    if (this.calibrateOnStart) {
      btn.classList.add("active");
      btn.innerHTML = '<i class="fas fa-check-square"></i>';
    } else {
      btn.classList.remove("active");
      btn.innerHTML = '<i class="far fa-square"></i>';
    }
  }

  async toggleTimer() {
    // Перевірка блокування через стан калібрування
    if (this.calibrationInProgress || !this.motorsHomed) {
      const msg = this.calibrationInProgress
        ? "Калібрування триває – зачекайте"
        : "Таймер не відкалібрований – виконайте калібрування";
      this.showToast(msg, "warning");
      return;
    }

    if (this.config.timerStopped) {
      if (this.calibrateOnStart) {
        this.showToast("Калібрування перед стартом...", "warning");
        try {
          await this.calibrateMotors();
          this.startTimerAfterCalibration();
        } catch {
          this.showToast("Калібрування не вдалося, запуск скасовано", "error");
        }
      } else {
        this.startTimerAfterCalibration();
      }
    } else {
      await this.stopTimerRequest();
    }
  }

  async stopTimerRequest() {
    try {
      const response = await fetch("/api/stop", { method: "POST" });
      if (response.ok) {
        const data = await response.json();
        this.config.timerStopped = data.status === "stopped";
        this.updateStartStopButton();
        this.showToast("Таймер зупинено", "warning");
        this.updateStartMomentDisplay();
        setTimeout(() => this.updateStatus(), 500);
      }
    } catch (error) {
      this.showToast("Помилка зупинки таймера", "error");
    }
  }

  async startTimerAfterCalibration() {
    try {
      const response = await fetch("/api/stop", { method: "POST" });
      if (response.ok) {
        const data = await response.json();
        this.config.timerStopped = data.status === "stopped";
        if (!this.config.timerStopped) {
          if (this.currentTime) {
            this.startMomentStatic = new Date(this.currentTime);
          } else {
            this.startMomentStatic = new Date();
          }
          this.calculateEndDate();
          // Скидаємо прапорець, щоб сповіщення про завершення могло з'явитися знову
          this.timeoutWarningShown = false;
        }
        this.updateStartStopButton();
        this.showToast(
          this.config.timerStopped ? "Таймер зупинено" : "Таймер запущено",
          this.config.timerStopped ? "warning" : "success",
        );
        this.updateStartMomentDisplay();
        setTimeout(() => this.updateStatus(), 500);
        this.hidePersistentNotification();
      }
    } catch (error) {
      this.showToast("Помилка запуску таймера", "error");
    }
  }

  async syncTime() {
    try {
      await fetch("/api/sync", { method: "POST" });
      this.showToast("Синхронізацію часу запущено", "success");
      setTimeout(() => this.updateStatus(), 2000);
    } catch (error) {
      this.showToast("Помилка синхронізації", "error");
    }
  }

  calibrateMotors() {
    return new Promise((resolve, reject) => {
      fetch("/api/calibrate", { method: "POST" })
        .then(async (res) => {
          if (!res.ok) throw new Error("Calibration start failed");
          this.showToast("Калібрування двигунів запущено", "warning");

          const checkInterval = setInterval(async () => {
            try {
              const stateRes = await fetch("/api/state");
              const stateData = await stateRes.json();
              if (!stateData.calibrationInProgress) {
                clearInterval(checkInterval);
                if (stateData.motorsHomed) {
                  this.showToast("Калібрування завершено успішно", "success");
                  resolve();
                } else {
                  this.showToast("Калібрування не вдалося", "error");
                  reject(new Error("Calibration failed"));
                }
              }
            } catch (e) {
              clearInterval(checkInterval);
              reject(e);
            }
          }, 500);
        })
        .catch((err) => {
          this.showToast("Помилка калібрування", "error");
          reject(err);
        });
    });
  }

  updateTestDigitsUI() {
    for (let i = 0; i < 4; i++) {
      const digitEl = document.querySelector(
        `.test-digit[data-segment="${i}"] .digit-number`,
      );
      if (digitEl) digitEl.textContent = this.testDigits[i];
    }
  }

  async startStatusUpdates() {
    await this.updateStatus();
    setInterval(() => this.updateStatus(), 1000);
  }

  async updateStatus() {
    try {
      const response = await fetch("/api/state");
      if (!response.ok) throw new Error("Network error");
      const data = await response.json();

      // Оновлюємо стан калібрування та моторів
      if (data.calibrationInProgress !== undefined) {
        this.calibrationInProgress = data.calibrationInProgress;
      }
      if (data.motorsHomed !== undefined) {
        this.motorsHomed = data.motorsHomed;
      }
      this.updateUIBlockedState();

      if (data.currentTimeFormatted) {
        document.getElementById("current-time").textContent =
          data.currentTimeFormatted;
        const [hours, minutes, seconds] = data.currentTimeFormatted
          .split(":")
          .map(Number);
        const now = new Date();
        now.setHours(hours, minutes, seconds, 0);
        this.currentTime = now;

        if (this.config.useCurrentOnStart && this.config.timerStopped) {
          this.calculateEndDate();
        }
      }

      if (data.timeRemaining) {
        document.getElementById("time-remaining").textContent =
          data.timeRemaining;
      }

      if (data.remainingSeconds !== undefined) {
        const detailed = this.formatDetailedRemaining(data.remainingSeconds);
        document.getElementById("time-remaining-detailed").textContent =
          detailed;
      }

      // ВИЯВЛЕННЯ ЗАВЕРШЕННЯ ВІДЛІКУ за рядком "Час вийшов"
      if (
        data.timeRemaining &&
        data.timeRemaining.trim() === "Час вийшов" &&
        !this.timeoutWarningShown
      ) {
        this.timeoutWarningShown = true;
        console.log("Countdown finished, showing notification");
        this.showCountdownFinishedNotification(data);
      }

      if (data.motorsHomed !== undefined) {
        this.updateCalibrationBadge(
          data.motorsHomed,
          data.calibrationInProgress,
        );
      }

      if (data.segmentValues) {
        const digits = document.querySelectorAll(".digit-sim");
        digits.forEach((digit, index) => {
          if (data.segmentValues[index] !== undefined) {
            const newValue = data.segmentValues[index];
            if (digit.textContent !== newValue.toString()) {
              digit.textContent = newValue;
              this.animateFlip(digit);
            }
          }
        });
      }

      if (data.timerStopped !== undefined) {
        this.config.timerStopped = data.timerStopped;
        this.updateStartStopButton();
        if (this.config.timerStopped) {
          this.checkPastEndDate();
        } else {
          this.hidePersistentNotification();
        }
      }

      if (data.startTimestamp && !this.config.timerStopped) {
        const serverStart = new Date(data.startTimestamp * 1000);
        if (
          !this.startMomentStatic ||
          this.startMomentStatic.getTime() !== serverStart.getTime()
        ) {
          this.startMomentStatic = serverStart;
        }
      }

      this.updateStartMomentDisplay();

      const statusElement = document.getElementById("connection-status");
      if (navigator.onLine) {
        statusElement.innerHTML = '<i class="fas fa-circle"></i> Підключено';
        statusElement.style.color = "#10b981";
      } else {
        statusElement.innerHTML = '<i class="fas fa-circle"></i> Не підключено';
        statusElement.style.color = "#dc2626";
      }
    } catch (error) {
      console.error("Помилка оновлення статусу:", error);
    }
  }

  // ========== ПЕРСИСТЕНТНЕ СПОВІЩЕННЯ ПРО ЗАВЕРШЕННЯ ==========
  showCountdownFinishedNotification(data) {
    const startTime = data.startTimestamp
      ? new Date(data.startTimestamp * 1000)
      : null;
    const endTime = new Date();
    const durationValue = data.durationValue;
    const durationUnit = data.durationUnit;

    let elapsedStr = "";
    if (startTime) {
      const diffMs = endTime - startTime;
      const totalSeconds = Math.floor(diffMs / 1000);
      elapsedStr = this.formatDetailedRemaining(totalSeconds);
    } else {
      elapsedStr = `${durationValue} ${durationUnit}`;
    }

    const startStr = startTime ? this.formatDateTime(startTime) : "невідомо";
    const endStr = this.formatDateTime(endTime);

    const message = `
      <strong>⏰ Відлік завершено!</strong><br><br>
      <strong>Початок:</strong> ${startStr}<br>
      <strong>Завершення:</strong> ${endStr}<br>
      <strong>Минуло:</strong> ${elapsedStr}
    `;

    this.showPersistentNotification("info", message);
  }

  updateCalibrationBadge(homed, inProgress) {
    const badge = document.getElementById("calibration-badge");
    const textSpan = document.getElementById("calibration-status-text");
    if (!badge || !textSpan) return;

    badge.classList.remove("calibrated", "not-calibrated", "in-progress");

    if (inProgress) {
      badge.classList.add("in-progress");
      textSpan.textContent = "Калібр: триває...";
      badge.querySelector("i").className = "fas fa-cogs fa-spin";
    } else if (homed) {
      badge.classList.add("calibrated");
      textSpan.textContent = "Калібр: OK";
      badge.querySelector("i").className = "fas fa-check-circle";
    } else {
      badge.classList.add("not-calibrated");
      textSpan.textContent = "Калібр: Ні";
      badge.querySelector("i").className = "fas fa-exclamation-triangle";
    }
  }

  showToast(message, type = "success", duration = 5000) {
    const container = document.getElementById("toast-container");
    const toast = document.createElement("div");
    toast.className = `toast ${type}`;
    let icon = "fas fa-check-circle";
    if (type === "error") icon = "fas fa-exclamation-circle";
    if (type === "warning") icon = "fas fa-exclamation-triangle";
    toast.innerHTML = `<i class="${icon}"></i><span>${message}</span>`;
    container.appendChild(toast);
    setTimeout(() => {
      toast.style.animation = "slideIn 0.3s ease reverse";
      setTimeout(() => container.removeChild(toast), 300);
    }, duration);
  }
}

document.addEventListener("DOMContentLoaded", () => {
  window.controller = new SplitFlopController();
});