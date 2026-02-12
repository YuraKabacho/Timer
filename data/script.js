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
    };

    this.testDigits = [0, 0, 0, 0];
    this.hourDigits = [0, 0];
    this.physicalUpdateTimers = [null, null, null, null];

    this.currentTime = null;
    this.endDateString = "Обчислюється...";
    this.startMomentStatic = null;
    this.timeoutWarningShown = false;

    this.init();
  }

  async init() {
    this.initEventListeners();
    this.initTestDigits();
    this.initHourDigits();
    this.initUnitSelector();
    this.initAutoSyncToggle();
    this.startStatusUpdates();
    await this.loadConfig();
    this.calculateEndDate();
    this.updateStartMomentDisplay();
    this.updateStartStopButton();

    document.getElementById("last-update").textContent =
      new Date().toLocaleDateString("uk-UA");

    setInterval(() => {
      console.log("Auto‑save (5 min)");
      this.saveConfig();
    }, 300000);
  }

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
            `.test-digit[data-segment="${i}"] .digit-number`
          );
          if (digitEl) digitEl.textContent = this.testDigits[i];
        }
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

      if (data.timerStopped !== undefined) {
        this.config.timerStopped = data.timerStopped;
        this.updateStartStopButton();
      }

      if (data.startTimestamp) {
        this.startMomentStatic = new Date(data.startTimestamp * 1000);
      }

      this.calculateEndDate();
      this.updateStartMomentDisplay();
    } catch (error) {
      console.error("Помилка завантаження конфігурації:", error);
    }
  }

  async saveConfig() {
    const durationStr = this.testDigits.join("");
    const durationValue = parseInt(durationStr, 10) || 0;
    this.config.durationValue = durationValue;

    const payload = {
      durationValue: durationValue,
      durationUnit: this.config.durationUnit,
      syncHour: this.config.syncHour,
      autoSync: this.config.autoSync,
      useCurrentOnStart: this.config.useCurrentOnStart,
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
        this.showToast("Налаштування збережено успішно", "success");
        await this.loadConfig();
      } else {
        const error = await response
          .json()
          .catch(() => ({ error: "Unknown error" }));
        throw new Error(error.error || "Помилка збереження");
      }
    } catch (error) {
      console.error("Save error:", error);
      this.showToast(error.message || "Помилка збереження налаштувань", "error");
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

  setPhysicalDigit(segment, value) {
    if (this.physicalUpdateTimers[segment]) {
      clearTimeout(this.physicalUpdateTimers[segment]);
    }
    this.physicalUpdateTimers[segment] = setTimeout(() => {
      fetch("/api/test", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: `segment=${segment}&value=${value}`,
      }).catch((err) => console.error("Physical update failed:", err));
    }, 200);
  }

  initTestDigits() {
    const testDigits = document.querySelectorAll(".test-digit");

    testDigits.forEach((digitElement, index) => {
      const digitNumber = digitElement.querySelector(".digit-number");
      const upBtn = digitElement.querySelector(".digit-btn.up");
      const downBtn = digitElement.querySelector(".digit-btn.down");

      digitNumber.addEventListener("click", () => {
        let newValue = parseInt(
          prompt(
            `Введіть цифру для сегмента ${index + 1} (0-9):`,
            this.testDigits[index]
          ),
          10
        );
        if (!isNaN(newValue) && newValue >= 0 && newValue <= 9) {
          this.testDigits[index] = newValue;
          digitNumber.textContent = newValue;
          this.animateFlip(digitNumber);
          this.calculateEndDate();

          if (this.config.timerStopped) {
            this.setPhysicalDigit(index, newValue);
          }
        }
      });

      upBtn.addEventListener("click", () => {
        this.testDigits[index] = (this.testDigits[index] + 1) % 10;
        digitNumber.textContent = this.testDigits[index];
        this.animateFlip(digitNumber);
        this.calculateEndDate();

        if (this.config.timerStopped) {
          this.setPhysicalDigit(index, this.testDigits[index]);
        }
      });

      downBtn.addEventListener("click", () => {
        this.testDigits[index] = (this.testDigits[index] - 1 + 10) % 10;
        digitNumber.textContent = this.testDigits[index];
        this.animateFlip(digitNumber);
        this.calculateEndDate();

        if (this.config.timerStopped) {
          this.setPhysicalDigit(index, this.testDigits[index]);
        }
      });
    });
  }

  initHourDigits() {
    // Прив'язка до нових елементів у панелі керування
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

    tenUp.addEventListener("click", () => {
      this.hourDigits[0] = (this.hourDigits[0] + 1) % 3;
      if (this.hourDigits[0] === 2 && this.hourDigits[1] > 3) {
        this.hourDigits[1] = 3;
      }
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
      this.saveConfig(); // автосохранение при зміні
    });

    tenDown.addEventListener("click", () => {
      this.hourDigits[0] = (this.hourDigits[0] - 1 + 3) % 3;
      if (this.hourDigits[0] === 2 && this.hourDigits[1] > 3) {
        this.hourDigits[1] = 3;
      }
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
      this.saveConfig();
    });

    oneUp.addEventListener("click", () => {
      if (this.hourDigits[0] === 2) {
        this.hourDigits[1] = (this.hourDigits[1] + 1) % 4;
      } else {
        this.hourDigits[1] = (this.hourDigits[1] + 1) % 10;
      }
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
      this.saveConfig();
    });

    oneDown.addEventListener("click", () => {
      if (this.hourDigits[0] === 2) {
        this.hourDigits[1] = (this.hourDigits[1] - 1 + 4) % 4;
      } else {
        this.hourDigits[1] = (this.hourDigits[1] - 1 + 10) % 10;
      }
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
      this.saveConfig();
    });

    tenDigit.addEventListener("click", () => this.promptHourDigit(0));
    oneDigit.addEventListener("click", () => this.promptHourDigit(1));
  }

  promptHourDigit(position) {
    let max = position === 0 ? 2 : this.hourDigits[0] === 2 ? 3 : 9;
    let newVal = parseInt(
      prompt(
        `Введіть цифру для ${position === 0 ? "десятків" : "одиниць"} години (0-${max}):`,
        this.hourDigits[position]
      ),
      10
    );
    if (!isNaN(newVal) && newVal >= 0 && newVal <= max) {
      this.hourDigits[position] = newVal;
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
      this.saveConfig();
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
      this.saveConfig();
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
        const unit = card.dataset.unit;
        this.config.durationUnit = unit;
        this.setActiveUnit(unit);
        this.calculateEndDate();

        if (!this.config.timerStopped) {
          await this.toggleTimer(); // зупиняємо таймер при зміні одиниць
        }
        this.saveConfig();
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
      startDateTime = this.currentTime ? new Date(this.currentTime) : new Date();
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
      document.getElementById("end-date-display").textContent = this.endDateString;
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

    document.getElementById("end-date-display").textContent = this.endDateString;
  }

  // ========== ВИПРАВЛЕНО: ДЕТАЛЬНЕ ФОРМАТУВАННЯ ДЛЯ ВЕЛИКИХ ЗНАЧЕНЬ ==========
  formatDetailedRemaining(seconds) {
    if (seconds <= 0) return "0 секунд";

    // Константи (рік = 365 днів, місяць = 30 днів)
    const SECONDS_IN_MINUTE = 60;
    const SECONDS_IN_HOUR = 3600;
    const SECONDS_IN_DAY = 86400;
    const SECONDS_IN_MONTH = 2592000; // 30 днів
    const SECONDS_IN_YEAR = 31536000;  // 365 днів

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
        this.saveConfig();
      });

    document
      .getElementById("btn-save")
      .addEventListener("click", () => this.saveConfig());
    document
      .getElementById("btn-sync")
      .addEventListener("click", () => this.syncTime());
    document
      .getElementById("btn-calibrate")
      .addEventListener("click", () => this.calibrateMotors());
    document
      .getElementById("btn-start-stop")
      .addEventListener("click", () => this.toggleTimer());
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

  async toggleTimer() {
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
        }

        this.updateStartStopButton();
        this.showToast(
          this.config.timerStopped ? "Таймер зупинено" : "Таймер запущено",
          this.config.timerStopped ? "warning" : "success"
        );
        this.updateStartMomentDisplay();
        setTimeout(() => this.updateStatus(), 500);
      }
    } catch (error) {
      this.showToast("Помилка перемикання таймера", "error");
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

  async calibrateMotors() {
    try {
      await fetch("/api/calibrate", { method: "POST" });
      this.showToast("Калібрування двигунів запущено", "warning");
      setTimeout(() => this.updateStatus(), 5000);
    } catch (error) {
      this.showToast("Помилка калібрування", "error");
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
        document.getElementById("time-remaining").textContent = data.timeRemaining;
      }

      if (data.remainingSeconds !== undefined) {
        const detailed = this.formatDetailedRemaining(data.remainingSeconds);
        document.getElementById("time-remaining-detailed").textContent = detailed;
      }

      if (data.timeRemaining === "Час вийшов" && !this.timeoutWarningShown) {
        this.timeoutWarningShown = true;
        this.showTimeoutWarning();
      } else if (data.timeRemaining !== "Час вийшов") {
        this.timeoutWarningShown = false;
      }

      if (data.motorsHomed !== undefined) {
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

      if (data.timerStopped !== undefined) {
        this.config.timerStopped = data.timerStopped;
        this.updateStartStopButton();
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

  showTimeoutWarning() {
    const message =
      "⏰ Час відліку вичерпано!\n\nВи можете:\n" +
      "• Увімкнути 'Запустити відлік від поточного моменту' та натиснути 'Старт';\n" +
      "• Або змінити дату/час старту або тривалість.";
    this.showToast(message, "warning", 10000);
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