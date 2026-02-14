<!DOCTYPE html>
<html lang="pl">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>24cxxprog - EEPROM Programmer</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; max-width: 1000px; margin: 0 auto; padding: 20px; background: #f6f8fa; }
        h1 { color: #0366d6; border-bottom: 3px solid #0366d6; padding-bottom: 10px; }
        .description { background: white; padding: 20px; border-radius: 6px; margin: 20px 0; }
        .screenshots { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 20px; margin: 30px 0; }
        .screenshot { background: white; padding: 10px; border-radius: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
        .screenshot img { width: 100%; border-radius: 4px; }
        .features { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin: 20px 0; }
        .feature-card { background: white; padding: 15px; border-radius: 6px; border-left: 4px solid #0366d6; }
        .feature-card h3 { margin-top: 0; color: #0366d6; }
        code { background: #f1f3f5; padding: 2px 6px; border-radius: 3px; }
    </style>
</head>
<body>
    <h1>🔧 24cxxprog - EEPROM 24Cxx Programmer</h1>
    
    <div class="description">
        <p><strong>A comprehensive EEPROM programmer application for the Flipper Zero</strong> that interfaces with 24Cxx series I2C memory chips. Supports read, write, erase and dump/restore operations with a user-friendly menu interface for all 24C series memory sizes.</p>
    </div>

    <h2>📸 Screenshots</h2>
    <div class="screenshots">
        <div class="screenshot"><img src="screenshots/1.png" alt="Screen 1"></div>
        <div class="screenshot"><img src="screenshots/2.png" alt="Screen 2"></div>
        <div class="screenshot"><img src="screenshots/3.png" alt="Screen 3"></div>
    </div>

    <h2>✨ Funkcje</h2>
    <div class="features">
        <div class="feature-card">
            <h3>📖 Operacje</h3>
            <ul>
                <li>Odczyt EEPROM</li>
                <li>Zapis danych</li>
                <li>Kasowanie pamięci</li>
                <li>Dump/Restore</li>
            </ul>
        </div>
        <div class="feature-card">
            <h3>🎮 Interfejs</h3>
            <ul>
                <li>Intuicyjne menu</li>
                <li>Feedback czasu rzeczywistego</li>
                <li>Kontrola navgacją</li>
                <li>Wszystkie rozmiary 24Cxx</li>
            </ul>
        </div>
    </div>

    <h2>⚙️ Obsługiwane Chipy</h2>
    <p>24C01, 24C02, 24C04, 24C08, 24C16, 24C32, 24C64, 24C128, 24C256, 24C512 i warianty kompatybilne</p>

    <h2>👤 Autor</h2>
    <p><a href="https://github.com/kamylwnb">@Dr.Mosfet</a></p>
</body>
</html>
