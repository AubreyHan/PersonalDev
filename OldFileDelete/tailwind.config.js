/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  darkMode: "media",
  theme: {
    extend: {
      colors: {
        mac: {
          sidebar: "rgba(246, 246, 246, 0.85)",
          sidebarDark: "rgba(30, 30, 30, 0.85)",
          border: "rgba(0, 0, 0, 0.08)",
          borderDark: "rgba(255, 255, 255, 0.12)",
          card: "#ffffff",
          cardDark: "#242424",
          cardHover: "#f8f9fa",
          cardHoverDark: "#2c2c2e",
          accent: "#007AFF",
          danger: "#FF3B30",
          success: "#34C759",
          warning: "#FF9500",
        }
      }
    },
  },
  plugins: [],
}
