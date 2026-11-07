// Clay-Konfigurationsseite. Nutzersichtbare Texte englisch.
// messageKey muss Zeichen fuer Zeichen zu package.json passen.
module.exports = [
  {
    "type": "heading",
    "defaultValue": "Tide"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Colours"
      },
      {
        "type": "color",
        "messageKey": "COLOR_BG",
        "defaultValue": "000000",
        "label": "Background"
      },
      {
        "type": "color",
        "messageKey": "COLOR_FG",
        "defaultValue": "FFFFFF",
        "label": "Hour and date"
      },
      {
        "type": "color",
        "messageKey": "COLOR_ACCENT",
        "defaultValue": "00AAFF",
        "label": "Accent colour"
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
