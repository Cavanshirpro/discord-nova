# discord-nova

C++ ile yazılmış, GUI ve TUI arayüzü olan bir Discord istemcisi.
İletişim için kendi [discord.cpp-self](https://github.com/Cavanshirpro/discord.cpp-self)
kütüphanemizi kullanıyoruz. Hedef tam bir Discord alternatifi; v0.1.0 bu hedefin
ilk metin istemcisi sürümü.

Bu sürümde sunucuları, metin kanallarını ve mevcut DM konuşmalarını görebilir,
son 100 mesajı okuyabilir ve mesaj gönderebilirsin. Canlı mesajlar, düzenlemeler
ve silmeler Gateway üzerinden güncellenir. Demo modu hesap istemez.

- GUI: Qt Widgets.
- Geliştirme TUI'si: `discord.cpp-mytesting/discord-nova` içindeki orijinal C++ arayüz.
  Sunucu şeridi, kategoriler, sohbet, arkadaşlar ve medya görünümü korunur.
- Platformlar: Windows, Linux ve macOS.
- GUI oturumu bellekte tutulur. TUI yerel token dosyasını okur; ayarlar ve taslaklar
  yerel `config.json` / `drafts.json` dosyalarında saklanır ve Git'e alınmaz.
- Plugin desteği bu sürümde yok; sonraki aşamada eklenecek.
- TUI ses/video/kayıt kontrolleri `NOVA_NATIVE_MEDIA=ON` derlemesi ve FFmpeg ister.
  Varsayılan taşınabilir paketler metin istemcisidir. Dosya yükleme henüz yok.

Geliştirme sürümü orijinal TUI'yi ve Chrome TLS profilini içerir; yayımlanmış
v0.1.0 paketleri önceki arayüzdür. [Orijinal TUI rehberi](TUI.md).

Windows'ta `start-demo.cmd` demoyu açar. `start-tui.cmd`, `config.json` ve
`test_token` dosyalarını kullanır. Mevcut kişisel dosyalarını GitHub'a yükleme.

[İlk sürümü indir](https://github.com/Cavanshirpro/discord-nova/releases/tag/v0.1.0).
Windows'ta normal kullanım için MSVC paketini seçebilirsin. Arşivin tamamını çıkar:

```powershell
./bin/discord-nova-gui.exe --demo
./bin/discord-nova-tui.exe --demo
```

Kaynak koddan derleme:

```powershell
# Önce ilgili MSYS2 bağımlılıklarını kur; ayrıntılar BUILDING.md içinde.
./scripts/build.ps1 -Toolchain mingw64
./scripts/build.ps1 -Toolchain ucrt64
./scripts/build.ps1 -Toolchain clang64
```

CMD için `scripts\build.cmd`, MSYS2/Linux/macOS için `sh scripts/build.sh`
kullanılır. MSVC, x64 Developer PowerShell veya Developer Command Prompt ister.
PowerShell ve CMD ayrı işletim sistemi hedefleri değildir; aynı native programı
başlatırlar. Derleme betikleri her kabuğun kullanım biçimini karşılar.

[Tam derleme rehberi](BUILDING.md) | [Yol haritası](../ROADMAP.md)

Kütüphane ile ilgili bir sorun çıkarsa [LIBRARY.md](LIBRARY.md) içinde takip
edilir; istemcinin içine kütüphanenin yerini alan ikinci bir Discord katmanı yazılmaz.
