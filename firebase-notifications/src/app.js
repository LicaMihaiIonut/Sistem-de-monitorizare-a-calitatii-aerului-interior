require('dotenv').config();

const open = require("open").default;
const express = require('express');
const path = require('path');
const admin = require('firebase-admin');

const serviceAccount = require(process.env.SERVICE_ACCOUNT_KEY_PATH);

/**
 * Initializes Firebase Admin using the service account credentials and
 * Firebase Realtime Database URL configured through environment variables.
 */
admin.initializeApp({
  credential: admin.credential.cert(serviceAccount),
  databaseURL: process.env.FIREBASE_DB_URL
});

const db = admin.database();

/**
 * Runtime state used to avoid sending repeated notifications
 * while the air quality remains in the same status level.
 */
let lastLevel = null;
let lastNotifiedValue = null;

/**
 * Stores the latest CO2 readings used to calculate a small rolling average.
 */
let buffer = [];

/**
 * Stores the latest humidity value received from Firebase.
 * Notifications are ignored until humidity is available.
 */
let currentHumidity = null;

/**
 * Default maximum CO2 threshold used as fallback until the configured value
 * is loaded from Firebase.
 */
let maxCO2 = 2000;

/**
 * Listens for humidity changes from Firebase and keeps the latest value in memory.
 */
db.ref("Sistem/umiditate").on("value", function (snap) {
  currentHumidity = snap.val();
  console.log("Humidity:", currentHumidity);
});

/**
 * Listens for CO2 threshold changes from Firebase.
 * If a valid numeric value is received, it replaces the default threshold.
 */
db.ref("Setare_Control/Nivel_max_CO2").on("value", function (snap) {
  const val = snap.val();

  if (val != null && !isNaN(val)) {
    maxCO2 = Number(val);
    console.log("Max CO2 level:", maxCO2);
  }
});

/**
 * Calculates the air quality status based on the current CO2 value
 * and the configured maximum CO2 threshold.
 *
 * @param {number} co2 - The CO2 value in ppm.
 * @returns {{level: string, emoji: string, text: string}} The air quality status.
 */
function getAirStatus(co2) {
  const p30 = 0.3 * maxCO2;
  const p60 = 0.6 * maxCO2;

  if (co2 >= 0 && co2 <= p30) {
    return { level: "bun", emoji: "🟢", text: "Aer curat" };
  }

  if (co2 > p30 && co2 <= p60) {
    return { level: "minor", emoji: "🟡", text: "Atentionare: Concentratia de CO2 intre (30%; 60%]" };
  }

  if (co2 > p60 && co2 <= maxCO2) {
    return { level: "mediu", emoji: "🟠", text: "Alerta minora: Concentratia de CO2 intre (60%; 100%]" };
  }

  if (co2 > maxCO2) {
    return { level: "periculos", emoji: "🔴", text: "Alerta majora: Aer periculos! Depasire prag maxim!" };
  }
}

/**
 * Listens for CO2 changes from Firebase.
 *
 * The raw CO2 value is validated, added to a small rolling buffer,
 * averaged, classified, and then used to send a notification only when
 * the air quality level changes.
 */
db.ref("Sistem/eco2").on("value", async function (snap) {
  const co2 = snap.val();
  console.log("CO2:", co2);

  if (currentHumidity === null) return;
  if (co2 < 100 || co2 > 8000) return;

  buffer.push(co2);

  if (buffer.length > 2) {
    buffer.shift();
  }

  const avg = Math.round(
    buffer.reduce((a, b) => a + b, 0) / buffer.length
  );

  const air = getAirStatus(avg);

  console.log("AVG:", avg, "| LEVEL:", air.level);

  /**
   * Sends a notification only when the air quality level changes.
   * This prevents repeated notifications for the same interval.
   */
  if (air.level !== lastLevel) {
    lastLevel = air.level;
    await sendNotification(avg, currentHumidity, air);
  }
});

/**
 * Sends a Firebase Cloud Messaging notification to the saved Web Push token.
 *
 * If the token is missing, the notification is skipped.
 * If the token is no longer valid, it is removed from Firebase.
 *
 * @param {number} co2 - The averaged CO2 value in ppm.
 * @param {number} humidity - The latest humidity value.
 * @param {{level: string, emoji: string, text: string}} air - The air quality status.
 * @returns {Promise<void>}
 */
async function sendNotification(co2, humidity, air) {
  try {
    const snap = await db.ref("tokensWebPush").once("value");
    const data = snap.val();

    if (!data || !data.token) {
      console.log("No token found");
      return;
    }

    const message = {
      token: data.token,
      data: {
        title: air.emoji + " Aer: " + air.level.toUpperCase(),
        body: "CO2: " + co2 + " ppm | Umiditate: " + humidity + "%\n" + air.text
      }
    };

    await admin.messaging().send(message);
    console.log("Notification sent");
  } catch (err) {
    if (err.code === "messaging/registration-token-not-registered") {
      console.log("Invalid token removed");
      await db.ref("tokensWebPush").remove();
    } else {
      console.error("Error:", err);
    }
  }
}

/**
 * Configures and starts the local Express server used to serve
 * the static frontend application.
 *
 * - exposes files from the "public" directory;
 * - serves index.html for the root route;
 * - serves 404.html for all unmatched routes;
 * - opens the app automatically in the default browser.
 */
const app = express();
const PORT = 3000;
const publicPath = path.join(__dirname, '..', 'public');

app.use(express.static(publicPath));

app.get('/', function (req, res) {
  res.sendFile(path.join(publicPath, 'index.html'));
});

app.use(function (req, res) {
  res.status(404).sendFile(path.join(publicPath, '404.html'));
});


app.listen(PORT, function () {
  console.log("Server started on http://localhost:" + PORT);
  open("http://localhost:" + PORT);
});