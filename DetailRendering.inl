// Opaque surfaces and brighter labels retain contrast on a light desktop.
const Color kDetailText(255, 247, 249, 253);
const Color kDetailLabel(255, 211, 219, 232);
const Color kDetailBody(255, 17, 22, 30);
const Color kDetailCard(255, 28, 36, 48);

std::wstring ShortSensorStatus(TemperatureState state) {
    switch (state) {
    case TemperatureState::Checking: return L"读取中";
    case TemperatureState::PermissionRequired:
    case TemperatureState::PermissionDenied: return L"未授权";
    case TemperatureState::DriverMissing: return L"缺少驱动";
    case TemperatureState::ComponentMissing: return L"缺少组件";
    case TemperatureState::SourceMissing: return L"无数据源";
    case TemperatureState::ReadError: return L"读取失败";
    case TemperatureState::Stale: return L"数据过期";
    default: return L"未提供";
    }
}

float DetailExtraHeight(int section) {
    const float height = section == 0 ? 396 : section == 1 ? 360 : section == 2 ?
        350 + 26 * static_cast<float>(std::min<size_t>(6, gValues.disks.size())) : 304;
    return height - kDetailsBaseHeight;
}

void DrawTrend(Gdiplus::Graphics& graphics, int section) {
    const auto& history = gHistory[section];
    FillRound(graphics, 20, 163, 292, 54, kCardRadius, Color(255, 24, 29, 38));
    Gdiplus::GraphicsPath trendCard;
    RoundedPath(trendCard, 20, 163, 292, 54, kCardRadius);
    Gdiplus::Pen trendBorder(Color(255, 42, 48, 60), 1);
    graphics.DrawPath(&trendBorder, &trendCard);
    Gdiplus::Pen grid(Color(255, 49, 52, 59), 1);
    graphics.DrawLine(&grid, 28.0f, 187.0f, 304.0f, 187.0f);
    if (history.size() < 2) {
        Text(graphics, L"正在采集趋势", 28, 169, 276, 38, 12, kDetailLabel);
        return;
    }
    const double maximum = section < 2 ? 100.0 : std::max(1.0, *std::max_element(history.begin(), history.end()));
    std::vector<Gdiplus::PointF> points;
    for (size_t i = 0; i < history.size(); ++i)
        points.emplace_back(28 + 276.0f * static_cast<float>(i) / static_cast<float>(history.size() - 1),
            210 - 39.0f * static_cast<float>(std::clamp(history[i] / maximum, 0.0, 1.0)));
    Gdiplus::GraphicsPath fill;
    fill.AddLines(points.data(), static_cast<INT>(points.size()));
    fill.AddLine(points.back(), Gdiplus::PointF(points.back().X, 211));
    fill.AddLine(Gdiplus::PointF(points.back().X, 211), Gdiplus::PointF(points.front().X, 211));
    fill.CloseFigure();
    Gdiplus::LinearGradientBrush shade(Gdiplus::Rect(28, 172, 276, 39),
        Color(72, 143, 181, 222), Color(4, 143, 181, 222), Gdiplus::LinearGradientModeVertical);
    graphics.FillPath(&shade, &fill);
    Gdiplus::Pen line(Accent(section), 1.8f);
    line.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawLines(&line, points.data(), static_cast<INT>(points.size()));
    Gdiplus::SolidBrush halo(Color(70, 143, 181, 222));
    graphics.FillEllipse(&halo, points.back().X - 6.0f, points.back().Y - 6.0f, 12.0f, 12.0f);
    Gdiplus::SolidBrush dot(Accent(section));
    graphics.FillEllipse(&dot, points.back().X - 2.5f, points.back().Y - 2.5f, 5.0f, 5.0f);
}

void DetailMetric(Gdiplus::Graphics& graphics, float x, float y, const std::wstring& label, const std::wstring& value) {
    FillRound(graphics, x, y, 140, 56, kCardRadius, kDetailCard);
    Gdiplus::GraphicsPath metricCard;
    RoundedPath(metricCard, x, y, 140, 56, kCardRadius);
    Gdiplus::Pen metricBorder(Color(255, 44, 52, 66), 1);
    graphics.DrawPath(&metricBorder, &metricCard);
    Text(graphics, label, x + 10, y + 5, 120, 20, 12, kDetailLabel);
    Text(graphics, value, x + 10, y + 25, 120, 26, 18, kDetailText, true);
}

void DrawDetails(Gdiplus::Graphics& graphics) {
    const int section = std::clamp(gDetailSection, 0, 3);
    const float height = kDetailsBaseHeight + DetailExtraHeight(section);
    FillRound(graphics, 0, 0, 332, height, kPopupRadius, kDetailBody);
    Gdiplus::GraphicsPath body;
    RoundedPath(body, 0, 0, 332, height, kPopupRadius);
    graphics.SetClip(&body);
    Gdiplus::LinearGradientBrush topGlow(Gdiplus::Rect(0, 0, 332, 96),
        Color(32, 34, 42, 54), Color(0, 17, 22, 30), Gdiplus::LinearGradientModeVertical);
    graphics.FillRectangle(&topGlow, 0, 0, 332, 96);
    graphics.ResetClip();
    const wchar_t* titles[] = {L"处理器", L"内存", L"磁盘读写", L"网络"};
    FillRound(graphics, 20, 19, 3, 20, 1.5f, Accent(section));
    Text(graphics, titles[section], 30, 16, 282, 28, 16, kDetailText, true);
    Gdiplus::Pen divider(Color(255, 42, 48, 62), 1);
    graphics.DrawLine(&divider, 20.0f, 44.0f, 312.0f, 44.0f);
    const std::wstring primary[] = {Percent(gValues.cpu), Percent(gValues.memory),
        gValues.diskAvailable ? FullRate(gValues.diskReadBytesPerSec + gValues.diskWriteBytesPerSec) : L"—",
        FullRate(gValues.downBytesPerSec + gValues.upBytesPerSec)};
    Text(graphics, primary[section], 18, 54, section < 2 ? 148 : 298, 54, section < 2 ? 40.0f : 28.0f, kDetailText, true);
    if (section < 2) {
        FillRound(graphics, 172, 58, 140, 76, kCardRadius, kDetailCard);
        Text(graphics, section == 0 ? L"当前最高核心" : L"可用内存", 182, 62, 120, 20, 12, kDetailLabel);
        Text(graphics, section == 0 ? Frequency(gValues.cpuClockMaximumMHz) :
            Amount(std::max(0.0, gValues.memoryTotalGiB - gValues.memoryUsedGiB)), 181, 82, 122, 31, 22, kDetailText, true);
        std::wstring status = L"尚未读到频率";
        bool boost = false;
        if (section == 1) status = L"总计 " + Amount(gValues.memoryTotalGiB);
        else if (gValues.cpuClockMaximumMHz > 0) {
            if (gValues.cpuBaseMHz <= 0) status = L"基准未知";
            else if (gValues.cpuClockMaximumMHz > gValues.cpuBaseMHz * 1.01) {
                status = L"睿频 +" + Frequency(gValues.cpuClockMaximumMHz - gValues.cpuBaseMHz); boost = true;
            } else status = L"基准以内";
        }
        Text(graphics, status, 182, 112, 120, 19, 12, boost ? Color(255, 162, 198, 173) : kDetailLabel);
    }
    const std::wstring descriptions[] = {L"当前利用率", L"已用 " + Amount(gValues.memoryUsedGiB),
        L"全部物理磁盘 · 读取 + 写入", L"Ethernet / Wi-Fi · 下载 + 上传"};
    Text(graphics, descriptions[section], 20, 112, section < 2 ? 140 : 292, 20, 12, kDetailLabel);
    const wchar_t* trends[] = {L"占用率趋势", L"占用率趋势", L"读写趋势", L"吞吐趋势"};
    Text(graphics, trends[section], 20, 139, 172, 20, 12, kDetailLabel);
    Text(graphics, std::to_wstring(gHistory[section].size()) + L" 次采样", 220, 139, 92, 20, 12, kDetailLabel, false, true);
    DrawTrend(graphics, section);
    if (section == 0) {
        DetailMetric(graphics, 20, 231, L"CPU 温度", gValues.cpuTemperature >= 0 ? Temperature(gValues.cpuTemperature) : ShortSensorStatus(gValues.cpuTemperatureState));
        DetailMetric(graphics, 172, 231, L"核心平均频率", Frequency(gValues.cpuClockAverageMHz));
        DetailMetric(graphics, 20, 295, L"标称基准", Frequency(gValues.cpuBaseMHz));
        DetailMetric(graphics, 172, 295, L"本次频率峰值", Frequency(gValues.cpuClockPeakMHz));
        const std::wstring note = gValues.cpuClockMaximumMHz <= 0 ? L"核心时钟暂不可用 · 右键重试硬件采温" :
            gValues.cpuRatedMaximumMHz > 0 ? L"硬件上限 " + Frequency(gValues.cpuRatedMaximumMHz) : L"峰值为本次采样记录";
        Text(graphics, note, 20, 363, 292, 20, 12, kDetailLabel);
    } else if (section == 1) {
        DetailMetric(graphics, 20, 231, L"已提交内存", gValues.memoryDetailsAvailable ? Amount(gValues.commitUsedGiB) : L"—");
        DetailMetric(graphics, 172, 231, L"系统缓存", gValues.memoryDetailsAvailable ? Amount(gValues.systemCacheGiB) : L"—");
        Text(graphics, L"提交上限", 20, 300, 132, 20, 12, kDetailLabel);
        Text(graphics, gValues.memoryDetailsAvailable ? Amount(gValues.commitLimitGiB) : L"—", 172, 300, 140, 20, 12, kDetailText, false, true);
        Text(graphics, L"内存温度", 20, 325, 132, 20, 12, kDetailLabel);
        Text(graphics, gValues.memoryTemperature >= 0 ? Temperature(gValues.memoryTemperature) : ShortSensorStatus(gValues.memoryTemperatureState),
            172, 325, 140, 20, 12, kDetailLabel, false, true);
    } else if (section == 2) {
        DetailMetric(graphics, 20, 231, L"读取速度", gValues.diskAvailable ? FullRate(gValues.diskReadBytesPerSec) : L"—");
        DetailMetric(graphics, 172, 231, L"写入速度", gValues.diskAvailable ? FullRate(gValues.diskWriteBytesPerSec) : L"—");
        Text(graphics, L"磁盘 / 分区", 20, 301, 120, 20, 12, kDetailLabel);
        Text(graphics, L"读取", 146, 301, 78, 20, 12, kDetailLabel, false, true);
        Text(graphics, L"写入", 230, 301, 82, 20, 12, kDetailLabel, false, true);
        for (size_t i = 0; i < std::min<size_t>(6, gValues.disks.size()); ++i) {
            const auto& disk = gValues.disks[i];
            const float y = 323 + static_cast<float>(i) * 26;
            Text(graphics, L"磁盘 " + disk.instance, 20, y, 120, 22, 12, kDetailText);
            Text(graphics, disk.readBytesPerSec < 0 ? L"—" : DiskRate(disk.readBytesPerSec), 146, y, 78, 22, 12, kDetailText, false, true);
            Text(graphics, disk.writeBytesPerSec < 0 ? L"—" : DiskRate(disk.writeBytesPerSec), 230, y, 82, 22, 12, kDetailText, false, true);
        }
        Text(graphics, gValues.disks.empty() ? L"尚无分盘数据" : gValues.disks.size() > 6 ? L"显示前 6 块 · 顶部为全部硬盘合计" : L"同一行盘符属于同一块物理硬盘", 20, height - 26, 292, 20, 12, kDetailLabel);
    } else {
        DetailMetric(graphics, 20, 231, L"下载速度", FullRate(gValues.downBytesPerSec));
        DetailMetric(graphics, 172, 231, L"上传速度", FullRate(gValues.upBytesPerSec));
    }
    Gdiplus::GraphicsPath outline;
    RoundedPath(outline, 0.5f, 0.5f, 331, height - 1, kPopupRadius - 0.5f);
    Gdiplus::Pen border(Color(170, 65, 68, 76), 1);
    graphics.DrawPath(&border, &outline);
}
