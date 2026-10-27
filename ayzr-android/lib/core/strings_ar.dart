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

  // History
  static const historyTitle = 'السجل';
  static const historyEmpty = 'لا يوجد سجل تحليلات بعد';
  static const headline = 'العنوان';

  // Settings / config
  static const settings = 'الإعدادات';
  static const backendUrl = 'عنوان الخادم';
  static const clientToken = 'رمز العميل';
  static const save = 'حفظ';
  static const saved = 'تم الحفظ';
  static const tokenHint = 'يُخزَّن على هذا الجهاز فقط';
}
