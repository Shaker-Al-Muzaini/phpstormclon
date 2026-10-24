# تشغيل ذكاء PHP والأدوات الجديدة

استمر العمل في نفس المشروع. لم تتغير إصدارات Qt أو MSVC أو Rust المطلوبة على Windows.

## سحب فرع التجربة وبناؤه

احفظ تغييراتك المحلية في commit أولًا إن أظهر `git status` ملفات معدّلة. لا تحذفها ولا تستخدم reset --hard.

```powershell
cd D:\laravelProject\PhpStorm222
git status
git fetch origin
git switch --track origin/codex/php-intelligence-tooling
.\tools\build-local.ps1 -QtRoot "C:\Qt\6.8.3\msvc2022_64" -Configuration Debug -Jobs 2 -SetupPhpTools -Run
```

إذا كان الفرع موجودًا عندك بالفعل:

```powershell
git switch codex/php-intelligence-tooling
git pull --ff-only origin codex/php-intelligence-tooling
```

استخدم مسار Qt المثبّت عندك. الخيار `-SetupPhpTools` يثبّت Intelephense 1.18.5 داخل بيانات التطبيق باستخدام Node.js 20+ وnpm الموجودين عندك، دون تثبيت عالمي أو تغيير Composer. للخادم رخصته الخاصة ولا تُضمّن نسخته داخل المستودع. يمكن البناء دون هذا الخيار ثم اختيار خادم موجود من Tools > PHP Intelligence. إعدادات الاتصال والتشخيص لا تستخدم خدمة ذكاء اصطناعي خارجية؛ تعطَّل إعدادات telemetry للخادم.

## أسرع بناء محلي بعد نجاح فحوص CI

أغلق نسخة Forge IDE المفتوحة قبل البناء، ثم بعد سحب الفرع:

```powershell
.\tools\build-local.ps1 -QtRoot "C:\Qt\6.8.3\msvc2022_64" -Configuration Debug -Jobs 4 -Fast -SetupPhpTools -Run
```

الخيار `-Fast` يستخدم ملفات البناء الموجودة ويبني التطبيق وخدمتيه المطلوبتين فقط، مع تجاوز إعادة تشغيل الاختبارات محليًا. فحوص CI تبقى كاملة. احذف `-SetupPhpTools` في التحديثات التالية عندما يكون خادم PHP مثبتًا بالفعل. لا تحذف مجلد build أو target، لأنهما يحتويان على ذاكرة البناء التي تسرّع التحديث.

## داخل البرنامج

افتح مجلد المشروع كاملًا الذي يحتوي على composer.json، ثم انتظر انتهاء الفهرسة الأولى. تُحفظ ذاكرة الفهرس لتسريع الفتح اللاحق. تثبيت Composer وتوفير vendor الخاص بمشروعك يبقيان مسؤولية بيئة مشروعك المعتادة.

| المطلوب | الاستخدام |
|---|---|
| إكمال PHP | الكتابة أو Ctrl+Space، ثم اختيار الاقتراح |
| الانتقال للتعريف | Ctrl+B أو F12 أو Ctrl+نقرة |
| إيجاد كلاس أو دالة | Ctrl+Alt+N |
| تنسيق PHP | Ctrl+Alt+L، مع إمكانية التراجع |
| إعادة الفهرسة | Tools > Reindex PHP Project |
| اقتراح الطرفية الرمادي | اكتب بداية أمر من السجل، ثم سهم اليمين لقبوله |
| إكمال مسار الطرفية | Tab أو Ctrl+Space؛ Enter يقبل الاقتراح المفتوح ولا ينفّذه |
| Git | قائمة Git: مقارنة التعديلات، تجهيز ملفات محددة، إلغاء التجهيز، ثم Commit للتغييرات المجهّزة |
| قواعد البيانات | Tools > Database؛ ابدأ بملف SQLite. انقر مرتين على الجدول لعرضه أو اكتب SQL |
| PHPStan / PHPCS | Tools > PHPStan / PHP_CodeSniffer؛ حدد php.exe وسكربت الأداة الموجود داخل vendor/bin أو ملف phar، واحفظ الملفات ثم شغّل الفحص |

PHPStan وPHPCS لا يُثبَّتان تلقائيًا داخل مشروعك. تُستخدم ملفات إعداداتهما الموجودة. عند عدم وجود إعداد PHPStan يُستخدم المستوى 5؛ تُعرض أخطاء الإعداد في لوحة المخرجات. تظهر النتائج في Problems مع المصدر والسطر والعمود عند توفرهما.

## اتصال قواعد البيانات

أمثلة (استبدل القيم؛ لا تحفظ كلمة المرور داخل ملفات المشروع):

```text
sqlite://C:/projects/app/database/database.sqlite
mysql://user@localhost/app?ssl-mode=disabled
postgresql://user@localhost/app?sslmode=disable
mysql://user@db.example.com/app?ssl-mode=verify_identity
postgresql://user@db.example.com/app?sslmode=verify-full
```

أدخل كلمة المرور في حقل Password. لا تُحفظ الاتصالات أو كلمات المرور على القرص. الاتصالات البعيدة تحتاج شهادة موثوقة؛ للشهادات الخاصة استخدم خيارات SQLx مثل ssl-ca في MySQL أو sslrootcert في PostgreSQL داخل الرابط. تعطيل TLS مقبول فقط على loopback. الوضع الافتراضي للقراءة فقط؛ أزل علامته وأعد الاتصال لتسمح بالكتابة.

تم اختبار SQLite فعليًا؛ مسارا MySQL وPostgreSQL مبنيان لكن اختبارهُما على خادم حي ما زال مطلوبًا. تُعرض أول 500 نتيجة بحد أقصى 4 MiB و15 ثانية للاستعلام. بعض الأنواع الخاصة تحتاج CAST إلى نص. إلغاء عملية كتابة أو انتهاء مهلتها يغلق الاتصال، لكنه لا يضمن أن الخادم لم ينفّذها؛ افحص النتيجة قبل إعادة تنفيذها.

الطرفية الحالية ليست محاكي TTY كاملًا. اقتراح المسارات يتتبع مجلد العمل من prompt الافتراضي؛ تخصيص prompt قد يمنع تحديثه. لا تُدّعى مطابقة كل ميزات IDE آخر، ولا إعادة هيكلة مدفوعة، ولا تشغيل جميع إضافات المحررات الأخرى.
