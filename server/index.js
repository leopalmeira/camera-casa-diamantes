import express from 'express';
import sharp from 'sharp';
import QRCode from 'qrcode';
import makeWASocket, { DisconnectReason, useMultiFileAuthState } from '@whiskeysockets/baileys';
import P from 'pino';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const app = express();
app.disable('x-powered-by');
const PORT = Number(process.env.PORT || 3000);
const ADMIN_USER = process.env.ADMIN_USER || '';
const ADMIN_PASSWORD = process.env.ADMIN_PASSWORD || '';
const DEVICE_TOKEN = process.env.DEVICE_TOKEN || '';
const DATA_DIR = process.env.DATA_DIR || path.resolve('data');
const PUBLIC_URL = (process.env.PUBLIC_URL || process.env.RENDER_EXTERNAL_URL || '').replace(/\/$/, '');
const cooldownMs = Math.max(30, Number(process.env.ALERT_COOLDOWN_SECONDS || 120)) * 1000;
const pixelThreshold = Math.max(10, Math.min(100, Number(process.env.PIXEL_THRESHOLD || 32)));
const motionThreshold = Math.max(0.01, Math.min(0.8, Number(process.env.MOTION_THRESHOLD || 0.13)));
if (!ADMIN_USER || !ADMIN_PASSWORD || !DEVICE_TOKEN || !PUBLIC_URL) {
  console.error('Configure ADMIN_USER, ADMIN_PASSWORD, DEVICE_TOKEN e PUBLIC_URL.');
  process.exit(1);
}
fs.mkdirSync(DATA_DIR, {recursive: true});
const contactsFile = path.join(DATA_DIR, 'contacts.json');
let contacts = [];
try { contacts = JSON.parse(fs.readFileSync(contactsFile, 'utf8')); } catch {}
if (!Array.isArray(contacts)) contacts = [];
let wa = null, waReady = false, waQr = null, waState = 'iniciando';
let connecting = false;
let latest = null, lastFrameAt = 0, lastMotionAt = 0, lastAlertAt = 0, motionScore = 0;
let prevPixels = null, busy = false, seenFrames = 0;
const clients = new Set();

function safeEquals(a,b) {
  const x = Buffer.from(String(a ?? '')); const y = Buffer.from(String(b ?? ''));
  return x.length === y.length && crypto.timingSafeEqual(x,y);
}
function admin(req,res,next) {
  res.set('Cache-Control','no-store');
  const v = req.headers.authorization || '';
  if (v.startsWith('Basic ')) {
    let decoded = '';
    try { decoded = Buffer.from(v.slice(6),'base64').toString('utf8'); } catch {}
    const cut = decoded.indexOf(':');
    if (cut >= 0 && safeEquals(decoded.slice(0,cut), ADMIN_USER) && safeEquals(decoded.slice(cut+1), ADMIN_PASSWORD)) return next();
  }
  res.set('WWW-Authenticate','Basic realm="ESP32-CAM Painel", charset="UTF-8"');
  return res.status(401).send('Autenticação necessária');
}
function device(req,res,next) {
  const token = (req.headers.authorization || '').replace(/^Bearer /i,'');
  return safeEquals(token, DEVICE_TOKEN) ? next() : res.sendStatus(401);
}
const logger = P({level: 'warn'});
async function connectWhatsApp() {
  if (connecting) return;
  connecting = true;
  try {
    const { state, saveCreds } = await useMultiFileAuthState(path.join(DATA_DIR, 'baileys_auth'));
    wa = makeWASocket({auth: state, logger, printQRInTerminal: false, markOnlineOnConnect: false, browser: ['ESP32-CAM', 'Chrome', '1.0']});
    wa.ev.on('creds.update', saveCreds);
    wa.ev.on('connection.update', ({connection, qr, lastDisconnect}) => {
      if (qr) {waQr = qr; waState = 'aguardando QR Code'; waReady = false;}
      if (connection === 'open') {waReady = true; waQr = null; waState = 'conectado'; console.log('WhatsApp conectado');}
      if (connection === 'close') {
        waReady = false; waQr = null;
        const code = lastDisconnect?.error?.output?.statusCode;
        waState = code === DisconnectReason.loggedOut ? 'desconectado: necessário vincular novamente' : 'reconectando';
        if (code !== DisconnectReason.loggedOut) setTimeout(() => connectWhatsApp(), 6000);
      }
    });
  } catch(err) {console.error('Baileys:',err.message); setTimeout(() => connectWhatsApp(),10000);}
  finally {connecting = false;}
}
connectWhatsApp();

function validNumber(value) {
  const s = String(value).replace(/\D/g,'');
  return /^\d{10,15}$/.test(s) ? s : null; // incluir DDI, ex.: 5521999999999
}
function saveContacts() {
  const tmp = contactsFile + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(contacts,null,2),{mode:0o600});
  fs.renameSync(tmp, contactsFile);
}
async function notifyMotion(jpg) {
  if (!waReady || !wa) {console.log('Movimento sem alerta: WhatsApp desconectado'); return;}
  const msg = `🚨 ESP32-CAM: movimento detectado!\nHorário: ${new Date().toLocaleString('pt-BR',{timeZone:'America/Sao_Paulo'})}\nCâmera: ${PUBLIC_URL}`;
  for (const number of contacts) {
    try {
      await wa.sendMessage(`${number}@s.whatsapp.net`, {image: jpg, caption: msg});
      console.log(`Alerta enviado para final ${number.slice(-4)}`);
    } catch (err) {console.error(`Falha de envio final ${number.slice(-4)}: ${err.message}`);}
  }
}
async function detectMotion(jpg) {
  // 32x24 px; escala de cinza: análise barata de variação entre quadros
  const pixels = await sharp(jpg).resize(32,24,{fit:'fill'}).greyscale().raw().toBuffer();
  if (prevPixels) {
    let changed=0;
    for (let i=0;i<pixels.length;i++) if(Math.abs(pixels[i]-prevPixels[i]) > pixelThreshold) changed++;
    motionScore = changed/pixels.length;
    // Exige 3 quadros de aquecimento; ignora mudanças globais extremas comuns à exposição/iluminação
    if (++seenFrames > 3 && motionScore >= motionThreshold && motionScore < 0.85) {
      lastMotionAt = Date.now();
      if (contacts.length && Date.now()-lastAlertAt >= cooldownMs) {
        lastAlertAt = Date.now();
        void notifyMotion(jpg);
      }
    }
  }
  prevPixels = pixels;
}

app.get('/health', (_req,res)=>res.json({ok:true}));
// Camera: endpoint separado, só aceita imagem e token.
app.post('/api/frame', device, express.raw({type:'image/jpeg',limit:'250kb'}), async(req,res)=>{
  if (!Buffer.isBuffer(req.body) || req.body.length<100 || req.body[0]!==0xff || req.body[1]!==0xd8) return res.sendStatus(415);
  if (busy) return res.sendStatus(202);
  busy=true;
  try {
    const {format,width,height} = await sharp(req.body).metadata();
    if (format!=='jpeg' || !width || !height || width>800 || height>600) return res.sendStatus(415);
    latest = Buffer.from(req.body); lastFrameAt = Date.now();
    const chunk = Buffer.concat([Buffer.from(`--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${latest.length}\r\n\r\n`),latest,Buffer.from('\r\n')]);
    for (const client of clients) {
      if (client.destroyed || client.writableLength>200000) {clients.delete(client); client.end();continue;}
      client.write(chunk);
    }
    try { await detectMotion(latest); } catch(e) {console.error('Detecção:',e.message);}
    res.sendStatus(204);
  } catch(e) {console.error('Quadro inválido:', e.message);res.sendStatus(422);}
  finally {busy=false;}
});

app.use(admin);
app.use(express.static(path.join(__dirname,'public'),{index:'index.html'}));
app.get('/api/status',(_req,res)=>res.json({online:Date.now()-lastFrameAt<10000,lastFrameAt,lastMotionAt,motionScore:Number(motionScore.toFixed(3)), whatsapp:waState, contacts:contacts.length}));
app.get('/api/contacts',(_req,res)=>res.json(contacts));
app.put('/api/contacts',express.json({limit:'10kb'}),(req,res)=>{
  if (!Array.isArray(req.body?.contacts) || req.body.contacts.length>15) return res.status(400).json({error:'Máximo 15 contatos'});
  const nums = req.body.contacts.map(validNumber);
  if (nums.some(n=>!n)) return res.status(400).json({error:'Use número com DDI: 5521999999999'});
  contacts = [...new Set(nums)];
  saveContacts();res.json({ok:true,contacts});
});
app.get('/api/whatsapp/qr',async(_req,res)=>{
  if (!waQr) return res.json({qr:null,status:waState});
  try {res.json({qr:await QRCode.toDataURL(waQr),status:waState});}
  catch {res.status(500).json({error:'QR indisponível'});}
});
app.get('/snapshot.jpg',(_req,res)=>{
  if (!latest) return res.sendStatus(503);
  res.set({'Content-Type':'image/jpeg','Cache-Control':'no-store'}).send(latest);
});
app.get('/stream', (req,res)=>{
  res.writeHead(200,{'Content-Type':'multipart/x-mixed-replace; boundary=frame','Cache-Control':'no-store, no-cache','Pragma':'no-cache','Connection':'keep-alive','X-Accel-Buffering':'no'});
  clients.add(res);
  if (latest) res.write(Buffer.concat([Buffer.from(`--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${latest.length}\r\n\r\n`),latest,Buffer.from('\r\n')]));
  const heartbeat=setInterval(()=>{if(!res.destroyed)res.write(': keepalive\r\n');},20000);
  req.on('close',()=>{clearInterval(heartbeat);clients.delete(res);});
});
app.listen(PORT,'0.0.0.0',()=>console.log(`Servidor ativo na porta ${PORT}`));
