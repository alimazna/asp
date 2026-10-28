/// Every user-facing string in the app, Arabic only. Centralized so the UI
/// never mixes languages and a reviewer can audit the wording in one place.
class Ar {
  const Ar._();

  // App / navigation
  static const appName = 'AYZR';
  static const appTagline = 'ذكاء سوق الذهب XAUUSD';
  static const navHome = 'الرئيسية';
  static const navAnalysis = 'التحليل';
  static const navNews = 'الأخبار';
  static const navHistory = 'السجل';

  // Common states
  static const loading = 'جارٍ التحميل…';
  static const retry = 'إعادة المحاولة';
  static const refresh = 'تحديث';
  static const unavailable = 'غير متاح';
  static const noData = 'لا توجد بيانات';
  static const empty = 'لا يوجد شيء لعرضه بعد';
  static const offline = 'تعذّر الاتصال بالخادم';
  static const offlineHint = 'تحقق من اتصال الشبكة ثم أعد المحاولة.';
  static const errorGeneric = 'حدث خطأ غير متوقع';
  static const stale = 'البيانات قديمة';
  static const staleHint = 'قد لا تعكس هذه القيم السوق الحالي.';
  static const degraded = 'النظام في وضع محدود';
  static const shadowOnly = 'وضع العرض فقط — لا تنفيذ لعمليات تداول';
  static const asOf = 'حتى';
  static const updatedAt = 'آخر تحديث';

  // Home
  static const homeTitle = 'الرئيسية';
  static const marketState = 'حالة السوق';
  static const systemHealth = 'حالة النظام';
  static const healthOk = 'سليم';
  static const healthDegraded = 'محدود';
  static const healthOffline = 'متوقف';
  static const bridgeLabel = 'جسر البيانات';
  static const latestSignal = 'أحدث إشارة';

  // Analysis
  static const analysisTitle = 'التحليل';
  static const contextSection = 'سياق السوق';
  static const signalSection = 'الإشارة';
  static const levelsSection = 'المستويات المقترحة';
  static const metaSection = 'بيانات وصفية';
  static const regime = 'النظام السوقي';
  static const h4Bias = 'انحياز H4';
  static const m15Trigger = 'محفّز M15';
  static const mtfAgreement = 'توافق الأطر الزمنية';
  static const volatility = 'التقلب';
  static const direction = 'الاتجاه';
  static const score = 'الدرجة';
  static const probability = 'الاحتمال المعاير';
  static const entry = 'الدخول';
  static const stopLoss = 'وقف الخسارة';
  static const takeProfit = 'الهدف';
  static const rewardRisk = 'المخاطرة/المكافأة';
  static const suggestedRisk = 'المخاطرة المقترحة';
  static const coverageTier = 'مستوى التغطية';
  static const dataFreshness = 'حداثة البيانات';
  static const disclaimerLabel = 'إخلاء المسؤولية';
  static const directionUp = 'صاعد';
  static const directionDown = 'هابط';
  static const directionFlat = 'عرضي';
  static const directionNone = 'لا إشارة';
  static const noSignal = 'لا توجد إشارة متاحة حالياً';
  static const scoreNotProbability =
      'هذه درجة غير معايرة وليست احتمالاً.';

  // News
  static const newsTitle = 'الأخبار';
  static const goldOnly = 'الذهب فقط';
  static const analysisStatus = 'حالة التحليل';
  static const statusAnalyzed = 'تم تحليله';
  static const statusNone = 'لم يُحلَّل';
  static const readMore = 'اقرأ المزيد';
  static const source = 'المصدر';
  static const articleDetails = 'تفاصيل الخبر';
  static const summary = 'الملخص';
  static const facts = 'الوقائع';
  static const marketImplications = 'التأثيرات على السوق';
  static const supportingEvidence = 'أدلة مؤيدة';
  static const opposingEvidence = 'أدلة معارضة';
  static const goldImpact = 'التأثير على الذهب';
  static const aiGoldRelevance = 'تصنيف الذكاء الاصطناعي لتأثير الخبر على الذهب';
  static const aiGoldRelevanceHint =
      'تقييم النموذج لمدى قدرة هذا الخبر على تحريك سعر الذهب فعلياً، '
      'وهو مستقل عن تصنيف مصدر الأخبار.';
  static const relHigh = 'مرتفع';
  static const relMedium = 'متوسط';
  static const relLow = 'منخفض';
  static const relNone = 'لا تأثير';
  static const relUnknown = 'غير محدَّد';
  static const invalidation = 'شروط الإبطال';
  static const limitations = 'القيود';
  static const astraAgreement = 'توافق مع تحليل ASTRA';
  static const timeframeContext = 'الأطر الزمنية';
  static const noAnalysisYet = 'لا يوجد تحليل لهذا الخبر بعد';
  static const analysisUnavailable = 'تعذّر تحليل الخبر';

  // AI provider quota / rate-limit notices. A spent daily quota and a temporary
  // burst limit are different situations and must read differently: the first is
  // a pause until the provider's window resets, the second resolves in seconds.
  static const quotaExhaustedTitle = 'توقف التحليل مؤقتاً';
  static const quotaExhaustedBody =
      'تم استهلاك حصة التحليل اليومية لمزوّد الذكاء الاصطناعي. '
      'سيعود التحليل تلقائياً بعد تجدّد الحصة.';
  static const quotaResetUnknown = 'موعد تجدّد الحصة غير معلوم حالياً.';
  static const quotaResetAt = 'يُتوقع تجدّد الحصة عند';
  static const rateLimitedTitle = 'المزوّد مشغول مؤقتاً';
  static const rateLimitedBody =
      'تعذّر التحليل الآن بسبب ضغط مؤقت على المزوّد. أعد المحاولة بعد لحظات.';

  // All-provider states. When every configured credential is down the banner
  // must NOT say "retry in a moment" — no amount of retrying clears a spent
  // budget or a rejected key. These read as a definite stop with the reason.
  static const allExhaustedTitle = 'توقّف التحليل — استُهلكت كل الحصص';
  static const allExhaustedBody =
      'تم استهلاك الحصة اليومية لدى جميع مزوّدي التحليل. '
      'لن يُستأنف التحليل حتى تجدّد الحصة.';
  static const allDownTitle = 'توقّف التحليل — لا يوجد مزوّد فعّال';
  static const allDownBody =
      'جميع مزوّدي التحليل غير متاحين حالياً. '
      'قد تكون المفاتيح منتهية أو مرفوضة، ويجب تحديثها من الخادم.';
  static const allDownReasonAuth = 'السبب: مفاتيح التحليل مرفوضة.';
  static const allDownReasonQuota = 'السبب: الحصص اليومية مستهلكة.';
  static const allDownReasonUnknown = 'السبب: تعذّر تحديد سبب دقيق.';
  static const degradedTitle = 'بعض مزوّدي التحليل غير متاحين';
  static const degradedBody =
      'التحليل يعمل عبر المزوّدين المتاحين. تم تخطّي المزوّدين غير المتاحين.';
  static const providersDownList = 'المزوّدون غير المتاحين';

  // History
  static const historyTitle = 'السجل';
  static const historyEmpty = 'لا يوجد سجل تحليلات بعد';
  static const headline = 'العنوان';

  // Macro events — scheduled economic releases and their expectation/result
  // turns. Kept distinct from the news surface: an event is known in advance.
  static const eventsTitle = 'الأحداث الاقتصادية';
  static const eventsSubtitle = 'إصدارات مجدولة وتوقعاتها';
  static const eventsEmpty = 'لا توجد أحداث مجدولة حالياً';
  static const eventUpcoming = 'القادمة';
  static const eventPast = 'المنقضية';
  static const eventConsensus = 'التوقع';
  static const eventPrevious = 'السابق';
  static const eventForecastUnavailable = 'لم يُنشر توقع';
  static const eventImpact = 'الأهمية';
  static const eventImpactHigh = 'مرتفعة';
  static const eventImpactMedium = 'متوسطة';
  static const eventImpactLow = 'منخفضة';
  static const eventImpactHoliday = 'عطلة';
  static const eventImpactUnknown = 'غير محدَّدة';
  static const eventStageScheduled = 'مجدول';
  static const eventStagePrepared = 'التوقع جاهز';
  static const eventStageReleased = 'صدر';
  static const eventExpectation = 'التوقع قبل الإصدار';
  static const eventResult = 'قراءة النتيجة';
  static const eventExpectationPending = 'لم يُكتب التوقع بعد';
  static const eventResultPending = 'لم تُقرأ النتيجة بعد';
  static const eventDetails = 'تفاصيل الحدث';
  static const eventScheduledAt = 'موعد الإصدار';
  static const eventGoldRelevant = 'ذو صلة بالذهب';

  // Activity log — the durable record of what the backend did.
  static const activityTitle = 'سجل النشاط';
  static const activityEmpty = 'لا يوجد نشاط مسجَّل بعد';
  static const activitySubtitle = 'ما نفّذه الخادم فعلياً، من الأحدث';
  static const activitySummary = 'ملخّص آخر ٢٤ ساعة';
  static const activityTotal = 'الإجمالي';
  static const outcomeOk = 'نجح';
  static const outcomeEmpty = 'لا نتائج';
  static const outcomeRejected = 'مرفوض';
  static const outcomeUnavailable = 'غير متاح';
  static const outcomeError = 'خطأ';
  static const kindNewsRead = 'قراءة الأخبار';
  static const kindCandleRead = 'قراءة الشموع';
  static const kindLlmAnalyze = 'تحليل ذكاء اصطناعي';
  static const kindNewsIngest = 'استلام أخبار';
  static const kindHistoryRead = 'قراءة السجل';
  static const filterAll = 'الكل';
  static const durationLabel = 'المدة';
  static const itemsLabel = 'العناصر';
  static const sourceLabel = 'المصدر';

  // AI provider chain status
  static const providersTitle = 'مزوّدو الذكاء الاصطناعي';
  static const providersSubtitle = 'أي مفاتيح التحليل تعمل فعلاً';
  static const activeProvider = 'المزوّد النشط';
  static const providerAttempts = 'المحاولات';
  static const providerSuccesses = 'الناجحة';
  static const providerFailures = 'الفاشلة';
  static const providerSkippedQuota = 'متخطّى (حصة منتهية)';
  static const providerWorking = 'يعمل';
  static const providerFailed = 'فاشل';
  static const providerUntried = 'لم يُجرَّب';
  static const providerNone = 'لا يوجد مزوّد مُهيّأ';
  static const providerNoneHint =
      'لم يُضبط أي مفتاح ذكاء اصطناعي على الخادم، لذا لا يمكن توليد تحليل إخباري.';

  // Settings / config
  static const settings = 'الإعدادات';
  static const backendUrl = 'عنوان الخادم';
  static const clientToken = 'رمز العميل';
  static const save = 'حفظ';
  static const saved = 'تم الحفظ';
  static const tokenHint = 'يُخزَّن على هذا الجهاز فقط';
}
