# Câmera Casa Diamantes

ESP32-CAM AI Thinker -> JPEG HTTPS -> Render Node.js. Painel em https://camera-casa-diamantes.onrender.com.

## Arduino IDE

O arquivo `firmware/ESP32CAM_Casa_Diamantes/ESP32CAM_Casa_Diamantes.ino` publicado aqui é **modelo sem segredos**.
Para gravar a placa, use a versão **privada**, com SSID, senha Wi-Fi e DEVICE_TOKEN definidos. Não publique a versão privada no GitHub.
Placa: `AI Thinker ESP32-CAM`. Porta COM do adaptador USB. Monitor Serial: 115200. ESP32 usa Wi-Fi 2,4 GHz.

## Render

Root directory: `server`; Build: `npm install`; Start: `npm start`.
Environment: `PUBLIC_URL=https://camera-casa-diamantes.onrender.com`, `ADMIN_USER`, `ADMIN_PASSWORD`, `DEVICE_TOKEN` (mesmo usado na placa), `DATA_DIR`. Nunca comitar credenciais.
`/health` é apenas diagnóstico; **não há ping para impedir suspensão**.

`render.yaml` mantém `plan: free` e armazenamento temporário; no Free, o serviço pode suspender e as fotos/contatos/sessão WhatsApp podem sumir no reinício. Não garante 15 dias. Para monitoramento permanente e retenção, contratar plano ativo com disco persistente, usando como referência `render-starter-persistente.example.yaml` e configurando `DATA_DIR=/var/data`. Revise os custos antes de aplicar.

## Site

Painel protegido por login HTTP Basic; stream MJPEG em `/stream`, quadro atual em `/snapshot.jpg`, foto de movimento salva a cada no mínimo 30 s; limpeza de fotos após 15 dias. Cadastro de contatos com nome, DDI e periodicidade 0 a 7 dias, lembretes na faixa das 10h (horário de São Paulo) e envio manual. Vincular WhatsApp por QR Code no painel. Baileys não é oficial e pode falhar. Não envie a senha do painel nos lembretes.

## Segurança

Crie senhas novas e um token aleatório forte no Render. Os valores exibidos anteriormente em capturas e no antigo render.yaml ficaram expostos: rotacione-os. Este protótipo usa `setInsecure()` no firmware para teste e não é adequado a exposição definitiva sem validação TLS. Respeite privacidade e autorização de monitoramento.
