importScripts('https://www.gstatic.com/firebasejs/10.0.0/firebase-app-compat.js');
importScripts('https://www.gstatic.com/firebasejs/10.0.0/firebase-messaging-compat.js');

firebase.initializeApp({
  apiKey: "AIzaSyDBtlip8qmHek6KQ0HIMJVP248k6_7YgL8",
  authDomain: "monitorizare-calitate-ae-bfdcf.firebaseapp.com",
  projectId: "monitorizare-calitate-ae-bfdcf",
  messagingSenderId: "171867470024",
  appId: "1:171867470024:web:4f6f982dec07bb15fd8dfd"
});

const messaging = firebase.messaging();

// Background notifications
messaging.onBackgroundMessage((payload) => {
  console.log('[SW] Message:', payload);

  const title = payload.data?.title || "Alerta CO2";
  const body = payload.data?.body || "Valoare detectata";

  self.registration.showNotification(title, {
    body,
    icon: "https://cdn-icons-png.flaticon.com/512/565/565547.png",
    tag: "co2-alert"
  });
});

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

console.log("Service Worker activ");