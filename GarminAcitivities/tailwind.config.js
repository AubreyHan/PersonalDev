/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        garmin: {
          blue: "#007CC3",
          dark: "#1A1A1A",
          cyan: "#00A3E0",
          card: "#242424",
          border: "#333333",
        }
      }
    },
  },
  plugins: [],
}
