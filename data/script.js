class SplitFlopController {
  constructor() {
    this.config = {
      startDate: "2026-01-01",
      startTime: "12:00:00",
      durationDays: 0,
      syncHour: 3,
      autoSync: true,
      timerStopped: false,
    };

    this.testDigits = [0, 0, 0, 0]; // чотири цифри тривалості
    this.hourDigits = [0, 0]; // [десятки годин, одиниці годин]

    this.init();
  }

  async init() {
    this.initEventListeners();
    this.initTestDigits();
    this.initHourDigits();
    this.startStatusUpdates();
    await this.loadConfig();
    this.calculateEndDate();

    document.getElementById("last-update").textContent =
      new Date().toLocaleDateString("uk-UA");
  }

  // ---- Завантаження конфігурації з сервера ----
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
      if (data.durationDays !== undefined) {
        this.config.durationDays = data.durationDays;
        const str = data.durationDays.toString().padStart(4, "0");
        for (let i = 0; i < 4; i++) {
          this.testDigits[i] = parseInt(str[i]);
          const digitEl = document.querySelector(
            `.test-digit[data-segment="${i}"] .digit-number`,
          );
          if (digitEl) digitEl.textContent = str[i];
        }
      }
      if (data.syncHour !== undefined) {
        this.config.syncHour = data.syncHour;
        this.hourDigits = [Math.floor(data.syncHour / 10), data.syncHour % 10];
        this.updateHourDisplay();
      }
      if (data.autoSync !== undefined) {
        this.config.autoSync = data.autoSync;
        document.getElementById("auto-sync").checked = data.autoSync;
      }

      this.calculateEndDate();
    } catch (error) {
      console.error("Помилка завантаження конфігурації:", error);
    }
  }

  // ---- Ініціалізація тестових цифр (тривалість) ----
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
            this.testDigits[index],
          ),
        );
        if (newValue >= 0 && newValue <= 9) {
          this.testDigits[index] = newValue;
          digitNumber.textContent = newValue;
          this.animateFlip(digitNumber);
          this.calculateEndDate();
        }
      });

      upBtn.addEventListener("click", () => {
        this.testDigits[index] = (this.testDigits[index] + 1) % 10;
        digitNumber.textContent = this.testDigits[index];
        this.animateFlip(digitNumber);
        this.calculateEndDate();
      });

      downBtn.addEventListener("click", () => {
        this.testDigits[index] = (this.testDigits[index] - 1 + 10) % 10;
        digitNumber.textContent = this.testDigits[index];
        this.animateFlip(digitNumber);
        this.calculateEndDate();
      });
    });
  }

  // ---- Ініціалізація селектора годин (NTP) ----
  initHourDigits() {
    const tenUp = document.getElementById("hour-ten-up");
    const tenDown = document.getElementById("hour-ten-down");
    const tenDigit = document.getElementById("hour-ten-digit");
    const oneUp = document.getElementById("hour-one-up");
    const oneDown = document.getElementById("hour-one-down");
    const oneDigit = document.getElementById("hour-one-digit");

    this.hourDigits = [0, 0];
    this.updateHourDisplay();

    tenUp.addEventListener("click", () => {
      this.hourDigits[0] = (this.hourDigits[0] + 1) % 3;
      if (this.hourDigits[0] === 2 && this.hourDigits[1] > 3) {
        this.hourDigits[1] = 3;
      }
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
    });

    tenDown.addEventListener("click", () => {
      this.hourDigits[0] = (this.hourDigits[0] - 1 + 3) % 3;
      if (this.hourDigits[0] === 2 && this.hourDigits[1] > 3) {
        this.hourDigits[1] = 3;
      }
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
    });

    oneUp.addEventListener("click", () => {
      if (this.hourDigits[0] === 2) {
        this.hourDigits[1] = (this.hourDigits[1] + 1) % 4;
      } else {
        this.hourDigits[1] = (this.hourDigits[1] + 1) % 10;
      }
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
    });

    oneDown.addEventListener("click", () => {
      if (this.hourDigits[0] === 2) {
        this.hourDigits[1] = (this.hourDigits[1] - 1 + 4) % 4;
      } else {
        this.hourDigits[1] = (this.hourDigits[1] - 1 + 10) % 10;
      }
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
    });

    tenDigit.addEventListener("click", () => this.promptHourDigit(0));
    oneDigit.addEventListener("click", () => this.promptHourDigit(1));
  }

  promptHourDigit(position) {
    let max = position === 0 ? 2 : this.hourDigits[0] === 2 ? 3 : 9;
    let newVal = parseInt(
      prompt(
        `Введіть цифру для ${position === 0 ? "десятків" : "одиниць"} години (0-${max}):`,
        this.hourDigits[position],
      ),
    );
    if (!isNaN(newVal) && newVal >= 0 && newVal <= max) {
      this.hourDigits[position] = newVal;
      this.updateHourDisplay();
      this.config.syncHour = this.hourDigits[0] * 10 + this.hourDigits[1];
    }
  }

  updateHourDisplay() {
    document.getElementById("hour-ten-digit").textContent = this.hourDigits[0];
    document.getElementById("hour-one-digit").textContent = this.hourDigits[1];
  }

  // ---- Анімація перекидання ----
  animateFlip(element) {
    element.style.animation = "flip 0.5s ease";
    setTimeout(() => (element.style.animation = ""), 500);
  }

  // ---- Обчислення дати завершення ----
  calculateEndDate() {
    const startDate = document.getElementById("start-date").value;
    const startTime = document.getElementById("start-time").value;
    const startDateTime = new Date(`${startDate}T${startTime}`);

    if (isNaN(startDateTime.getTime())) {
      document.getElementById("end-date-display").textContent = "Невірна дата";
      return;
    }

    const durationStr = this.testDigits.join("");
    const durationDays = parseInt(durationStr, 10) || 0;
    this.config.durationDays = durationDays;

    let endDateTime = new Date(startDateTime);
    endDateTime.setDate(endDateTime.getDate() + durationDays);

    const formattedDate = endDateTime.toLocaleDateString("uk-UA", {
      year: "numeric",
      month: "long",
      day: "numeric",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });

    document.getElementById("end-date-display").textContent = formattedDate;
  }

  // ---- Ініціалізація подій ----
  initEventListeners() {
    document
      .getElementById("start-date")
      .addEventListener("change", () => this.calculateEndDate());
    document
      .getElementById("start-time")
      .addEventListener("change", () => this.calculateEndDate());

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
      .getElementById("btn-stop")
      .addEventListener("click", () => this.stopTimer());
    document
      .getElementById("btn-apply-test")
      .addEventListener("click", () => this.applyTestDigits());

    document.getElementById("auto-sync").addEventListener("change", (e) => {
      this.config.autoSync = e.target.checked;
    });
  }

  // ---- Збереження конфігурації ----
  async saveConfig() {
    const durationStr = this.testDigits.join("");
    const durationDays = parseInt(durationStr, 10) || 0;

    const payload = {
      startDate: document.getElementById("start-date").value,
      startTime: document.getElementById("start-time").value,
      durationDays: durationDays,
      syncHour: this.config.syncHour,
      autoSync: this.config.autoSync,
    };

    try {
      const response = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });

      if (response.ok) {
        this.showToast("Налаштування збережено успішно", "success");
        this.config.durationDays = durationDays;
      } else {
        const error = await response.json();
        throw new Error(error.error || "Помилка збереження");
      }
    } catch (error) {
      this.showToast(
        error.message || "Помилка збереження налаштувань",
        "error",
      );
      console.error(error);
    }
  }

  // ---- Тестування цифр (фізичне встановлення) ----
  async applyTestDigits() {
    try {
      for (let i = 0; i < 4; i++) {
        const response = await fetch("/api/test", {
          method: "POST",
          headers: { "Content-Type": "application/x-www-form-urlencoded" },
          body: `segment=${i}&value=${this.testDigits[i]}`,
        });
        if (!response.ok) throw new Error("Помилка встановлення цифр");
      }

      const digitSims = document.querySelectorAll(".digit-sim");
      digitSims.forEach((sim, i) => {
        sim.textContent = this.testDigits[i];
        this.animateFlip(sim);
      });

      this.showToast(
        `Встановлено цифри: ${this.testDigits.join("")}`,
        "success",
      );
    } catch (error) {
      this.showToast("Помилка встановлення цифр", "error");
    }
  }

  // ---- Синхронізація часу ----
  async syncTime() {
    try {
      const response = await fetch("/api/sync", { method: "POST" });
      if (response.ok) {
        this.showToast("Синхронізацію часу запущено", "success");
        setTimeout(() => this.updateStatus(), 2000);
      }
    } catch (error) {
      this.showToast("Помилка синхронізації", "error");
    }
  }

  // ---- Калібрування двигунів ----
  async calibrateMotors() {
    try {
      const response = await fetch("/api/calibrate", { method: "POST" });
      if (response.ok) {
        this.showToast("Калібрування двигунів запущено", "warning");
        // Через деякий час оновимо статус – після калібрування сегменти будуть у нулі
        setTimeout(() => this.updateStatus(), 5000);
      }
    } catch (error) {
      this.showToast("Помилка калібрування", "error");
    }
  }

  // ---- Зупинка / запуск таймера ----
  async stopTimer() {
    try {
      const response = await fetch("/api/stop", { method: "POST" });
      if (response.ok) {
        const data = await response.json();
        this.config.timerStopped = data.status === "stopped";
        document.getElementById("timer-status").textContent = this.config
          .timerStopped
          ? "Зупинений"
          : "Активний";
        document.getElementById("timer-status").style.color = this.config
          .timerStopped
          ? "#f59e0b"
          : "#10b981";
        this.showToast(
          this.config.timerStopped ? "Таймер зупинено" : "Таймер запущено",
          this.config.timerStopped ? "warning" : "success",
        );
      }
    } catch (error) {
      this.showToast("Помилка зупинки таймера", "error");
    }
  }

  // ---- Оновлення статусу з сервера (періодичне) ----
  async startStatusUpdates() {
    await this.updateStatus();
    setInterval(() => this.updateStatus(), 3000);
  }

  async updateStatus() {
    try {
      const response = await fetch("/api/state");
      if (!response.ok) throw new Error("Network error");
      const data = await response.json();

      if (data.currentTimeFormatted)
        document.getElementById("current-time").textContent =
          data.currentTimeFormatted;
      if (data.timeRemaining)
        document.getElementById("time-remaining").textContent =
          data.timeRemaining;

      if (data.motorsHomed !== undefined) {
        const status = data.motorsHomed ? "Калібровані" : "Не калібровані";
        document.getElementById("motor-status").textContent = status;
        document.getElementById("motor-status").style.color = data.motorsHomed
          ? "#10b981"
          : "#f59e0b";
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
        document.getElementById("timer-status").textContent = data.timerStopped
          ? "Зупинений"
          : "Активний";
        document.getElementById("timer-status").style.color = data.timerStopped
          ? "#f59e0b"
          : "#10b981";
      }

      if (data.endDateDisplay) {
        document.getElementById("end-date-display").textContent =
          data.endDateDisplay;
      }

      // Якщо таймер зупинено – показуємо в тестових цифрах збережену тривалість
      if (data.timerStopped && data.durationDays !== undefined) {
        const str = data.durationDays.toString().padStart(4, "0");
        for (let i = 0; i < 4; i++) {
          this.testDigits[i] = parseInt(str[i]);
          const testDigit = document.querySelector(
            `.test-digit[data-segment="${i}"] .digit-number`,
          );
          if (testDigit) testDigit.textContent = str[i];
        }
      }

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

  showToast(message, type = "success") {
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
    }, 5000);
  }
}

document.addEventListener("DOMContentLoaded", () => {
  window.controller = new SplitFlopController();
});
