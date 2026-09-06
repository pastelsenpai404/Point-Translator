// Local deterministic fixture. Never contacts an external provider.
const http = require('node:http');
const payload = {
  karaoke: 'หนี่ ห่าว', thai: 'สวัสดี วันนี้ว่างไหม?', explanation: 'ถามเวลาว่างอย่างเป็นกันเอง',
  words: [{word:'你好',pinyin:'nǐ hǎo',karaoke:'หนี่ ห่าว',meaning:'สวัสดี',note:'คำทักทาย'}],
  replies: [
    {tone:'สุภาพ',text:'您好，请问有什么事？',thai:'สวัสดีครับ มีเรื่องอะไรหรือครับ?'},
    {tone:'เป็นกันเอง',text:'嗨，怎么啦？',thai:'ไง มีอะไรเหรอ?'},
    {tone:'ถามต่อ',text:'你想约几点？',thai:'อยากนัดกี่โมงเหรอ?'}
  ]
};
http.createServer((req,res) => {
  let body='';
  req.on('data',chunk=>body+=chunk);
  req.on('end',()=>{
    try {
      const request=JSON.parse(body);
      if(req.url!=='/v1/chat/completions' || !request.messages[1].content.includes('exactly three')) throw Error('Invalid request');
      if(body.includes('HTTP_ERROR')) { res.writeHead(503); res.end('{"message":"Test unavailable"}'); return; }
      const content={...payload};
      const target = request.messages[1].content.match(/TARGET language: (zh|en|th)/)?.[1];
      content.translated = {zh:'你好，今天有空吗？',en:'Hello, are you free today?',th:payload.thai}[target];
      content.original_pinyin = 'nǐ hǎo';
      content.translated_pinyin = target === 'zh' ? 'nǐ hǎo' : '';
      content.replies = payload.replies.map(reply => ({...reply, pinyin: 'nǐ hǎo'}));
      if(body.includes('NO_REPLIES')) delete content.replies;
      res.setHeader('Content-Type','application/json');
      res.end(JSON.stringify({choices:[{message:{content:JSON.stringify(content)}}]}));
    } catch(error) { res.writeHead(400); res.end(JSON.stringify({message:error.message})); }
  });
}).listen(18764,'127.0.0.1',()=>console.log('Mock provider listening on 18764'));
