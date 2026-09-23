/*******************************************************************************
 * 文件: OverlayScene.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   覆盖层场景的核心实现文件。实现输入事件处理、动画状态更新、以及完整的
 *   2D 场景绘制逻辑。绘制内容包括：光标指示（圆环/光晕/实心圆）、点击涟漪扩散、
 *   激光笔轨迹（二次贝塞尔插值平滑曲线）、屏幕绘图标注（矩形/椭圆/箭头）、
 *   按键和弦显示（圆角矩形按键标签）、鼠标状态面板（左右键/滚轮/侧键指示）。
 *   算法复杂度：绘制 O(N)，N 为特效元素数；涟漪/激光/框均有数量上限。
 *   动画使用 Smoothstep 缓动函数（t*t*(3-2t)）实现自然的淡入淡出。
 *   空间-时间权衡：激光轨迹使用二次插值将每段分为 2 条子线段，
 *   以少量额外顶点换取平滑曲线视觉效果。
 *
 * 设计哲学:
 *   采用命令模式：Paint() 方法通过 Painter 构建 DrawList，不直接渲染，
 *   由 OverlayManager 统一提交到 Vulkan。
 *   特效元素使用 deque 队列管理，过期元素自动头部弹出，限制最大数量。
 *   按键面板自动定位：跟随光标，空间不足时翻转到上方，确保不超出屏幕。
 *
 * 技术特性:
 *   - Smoothstep 缓动: t*t*(3-2t) 实现平滑的淡入淡出曲线。
 *   - 二次贝塞尔激光: 通过中点插值实现平滑激光轨迹，支持发光外层与核心内层。
 *   - 自动定位: 按键面板根据光标位置和屏幕空间自动选择上下方位。
 *   - 排除机制: 可配置排除特定热键组合不显示在覆盖层上。
 *   - 多形状标注: 支持矩形、椭圆、箭头三种绘图标注形状。
 *
 * 依赖关系:
 *   内部: ih::features::OverlayScene、ih::render2d (Color, Painter, DrawList)、
 *         ih::input (Key, Event, State, Chord)、ih::SettingsModel、ih::text::TextEngine
 *   外部: <algorithm> (std::clamp), <cmath> (std::hypot, std::sin/cos)
 *
 * 注意事项:
 *   - 所有坐标为桌面物理像素，Paint 内部按 DPI 缩放到逻辑坐标。
 *   - 激光轨迹最大 512 点，涟漪最大 32 个，绘图框最大 64 个。
 *
 ******************************************************************************/
// OverlayScene 头文件
#include "Features/OverlayScene.h"
// 颜色工具：Hex() 解析十六进制颜色字符串
#include "Render2D/Color.h"
// 标准算法：std::clamp
#include <algorithm>
// 数学库：std::hypot, std::sin/cos, std::pow
#include <cmath>
// ih::features 命名空间
namespace ih::features {
// 简化 render2d 命名空间使用
using namespace render2d;
// 匿名命名空间：以下辅助函数仅本编译单元可见
namespace {
// Smooth — Smoothstep 缓动函数，输入 t∈[0,1]，输出平滑过渡值
// 公式: t*t*(3-2t)，在端点处导数为零，实现自然的淡入淡出
float Smooth(float t) { t = std::clamp(t,0.f,1.f); return t * t * (3 - 2 * t); }
// Fade — 计算元素透明度：age<=hold 时完全不透明，之后线性淡出
// age=当前时间-出生时间, hold=保持时间, duration=淡出时长
float Fade(double age, double hold, double duration) { return age <= hold ? 1.f : duration <= 0 ? 0.f : 1 - Smooth(static_cast<float>((age-hold)/duration)); }
// Mix — 两点线性插值，t∈[0,1]
Point Mix(Point a, Point b, float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}; }
// 匿名命名空间结束
}
// ========================================================================
// Anchor — 根据锚点字符串计算元素位置
// 根据 left/right/top/bottom 组合与偏移量，将元素对齐到指定边缘
// ========================================================================
Point Anchor(std::string_view anchor, float width, float height, float itemWidth, float itemHeight, float x, float y) {
// 默认水平居中
    float px = (width-itemWidth)*.5f;
// 左对齐（左边距 24px）
    if (anchor.ends_with("left")) px = 24;
// 右对齐（右边距 24px）
    if (anchor.ends_with("right")) px = width-itemWidth-24;
// 垂直：top 开头则从上方偏移，否则从下方偏移
    float py = anchor.starts_with("top") ? y : height-itemHeight-y;
// 钳位到屏幕范围内
    return {std::clamp(px+x,0.f,std::max(0.f,width-itemWidth)),std::clamp(py,0.f,std::max(0.f,height-itemHeight))};
}
// Reset — 重置场景：清空所有特效元素与输入状态
void OverlayScene::Reset() { m_input.Reset(); m_ripples.clear(); m_laser.clear(); m_laserHeld=false;m_boxes.clear();m_drawing=false; }
// DrawingActive — 查询绘图模式是否激活
// 条件：绘图功能已启用 && 绘图热键当前被按住
bool OverlayScene::DrawingActive(const SettingsModel& settings)const{
    return settings.values["drawing"]["enabled"].get<bool>()&&m_input.Held(input::ParseHotkey(settings.values["drawing"]["hotkey"].get<std::string>()));
}
// ========================================================================
// Apply — 接收原始输入事件，更新场景状态
// 处理：重置事件、右键撤销绘图、左键开始/结束绘图框、点击涟漪生成
// ========================================================================
void OverlayScene::Apply(const input::Event& event, const SettingsModel& settings) {
// 重置事件：清空整个场景
    if (event.type == input::EventType::Reset) { Reset(); return; }
// 记录按键是否已处于按下状态（区分首次按下与重复）
    const bool repeated=m_input.Down(event.key);
// 更新输入状态机（考虑是否仅响应快捷键）
    m_input.Apply(event, settings.values["input"]["shortcut_only"].get<bool>());
// 非绘图模式时清空所有绘图框
    if(!DrawingActive(settings)){m_boxes.clear();m_drawing=false;}
// 绘图模式下右键按下：撤销最后一个绘图框
    else if(event.type==input::EventType::Down&&!repeated&&event.key==input::Key::MouseRight){
        if(!m_boxes.empty())m_boxes.pop_back();m_drawing=false;
    }
// 左键处理：开始新框 / 结束当前框
    else if(event.key==input::Key::MouseLeft){
// 左键按下且正在拖曳：创建新框（限制最多 64 个）
        if(event.type==input::EventType::Down&&!m_drawing&&m_boxes.size()<64){m_boxes.push_back({event.position,event.position,settings.values["drawing"]["shape"].get<std::string>()});m_drawing=true;}
// 左键释放：完成当前框
        if(event.type==input::EventType::Up&&m_drawing){m_boxes.back().end=event.position;m_drawing=false;}
    }
// 非绘图模式下鼠标按键按下时生成点击涟漪（限左键/右键/中键）
    if (!DrawingActive(settings)&&event.type == input::EventType::Down && event.key >= input::Key::MouseLeft && event.key <= input::Key::MouseMiddle &&
        settings.values["cursor"]["click_ripple"].get<bool>()) {
// 根据按键选择颜色（右键色或左键色）
        const auto key = event.key == input::Key::MouseRight ? "right_color" : "left_color";
// 解析颜色字符串
        auto color = Hex(settings.values["cursor"][key].get<std::string>());
// 中键使用默认灰白色
        if (event.key == input::Key::MouseMiddle) color = {.9f,.92f,.95f,1};
// 添加涟漪到队列
        m_ripples.push_back({event.position,color,event.time});
// 限制涟漪最大数量 32
        while (m_ripples.size()>32) m_ripples.pop_front();
    }
}
// ========================================================================
// Update — 推进动画状态，清理过期元素
// 参数: now=当前时间戳, cursor=当前光标位置, settings=用户设置
// ========================================================================
void OverlayScene::Update(double now, Point cursor, const SettingsModel& settings) {
// 更新缓存的光标位置
    m_cursor=cursor;
// 非绘图模式时清空绘图框
    if(!DrawingActive(settings)){m_boxes.clear();m_drawing=false;}
// 绘图模式下实时更新当前拖曳框的终点
    else if(m_drawing&&!m_boxes.empty())m_boxes.back().end=cursor;
// 获取输入相关设置
    const auto& input = settings.values["input"];
// 过期输入状态（超过 hold+fade 时间后清理）
    m_input.Expire(now,(input["hold_ms"].get<double>()+input["fade_ms"].get<double>())/1000);
// 清理过期涟漪（寿命 320ms）
    while (!m_ripples.empty() && now-m_ripples.front().born>=.32) m_ripples.pop_front();
// 若涟漪功能被禁用则清空
    if (!settings.values["cursor"]["click_ripple"].get<bool>()) m_ripples.clear();
// 获取激光笔相关设置
    const auto& laser=settings.values["laser"];
// 激光轨迹寿命
    const double lifetime=laser["fade_ms"].get<double>()/1000;
// 清理过期激光轨迹点
    while (!m_laser.empty() && now-m_laser.front().born>=lifetime) m_laser.pop_front();
// 解析激光热键
    const auto hotkey=input::ParseHotkey(laser["hotkey"].get<std::string>());
// 激光是否激活：启用 && 热键按住 && 非绘图模式
    const bool active=laser["enabled"].get<bool>() && m_input.Held(hotkey)&&!DrawingActive(settings);
// 绘图模式激活时清空激光
    if(DrawingActive(settings))m_laser.clear();
// 激光功能禁用时清空
    if (!laser["enabled"].get<bool>()) m_laser.clear();
// 激光刚激活时递增笔画编号（区分连续笔画）
    if (active && !m_laserHeld) ++m_stroke;
// 记录激光热键是否按住
    m_laserHeld=active;
// 轨迹模式：在光标处追加轨迹点
    if (active && laser["mode"]=="trail") {
// 获取上一个轨迹点位置
        const auto last=m_laser.empty()?cursor:m_laser.back().position;
// 距离超过 1.5px 或间隔超过 8ms 时才追加新点（防止过密）
        if (m_laser.empty() || m_laser.back().stroke!=m_stroke || std::hypot(cursor.x-last.x,cursor.y-last.y)>=1.5f || now-m_laser.back().born>=.008) {
// 追加轨迹点
            m_laser.push_back({cursor,now,m_stroke});
// 限制激光轨迹最大 512 点
            while(m_laser.size()>512) m_laser.pop_front();
        }
    }
// 非轨迹模式时清空激光
    if (laser["mode"]!="trail") m_laser.clear();
}
// ========================================================================
// Animating — 查询是否有任何动画正在播放
// 返回 true 时主循环应持续重绘
// ========================================================================
bool OverlayScene::Animating(double now,const SettingsModel& settings) const {
// 获取输入设置
    const auto& input=settings.values["input"];
// 判断条件：绘图中 || 涟漪非空 || 激光非空 || 激光按住 ||
// 键盘和弦显示中 || 鼠标按下中或刚释放或滚轮刚发生
    return m_drawing || !m_ripples.empty() || !m_laser.empty() || m_laserHeld ||
        (input["keyboard_enabled"].get<bool>() && !m_input.Chords().empty()) ||
        (input["mouse_enabled"].get<bool>() && (m_input.AnyMouse() || now-m_input.mouseReleased<1 || now-m_input.wheelTime<.75));
}
// ========================================================================
// PaintCursor — 绘制光标指示（静态方法）
// 支持 ring（圆环）/ halo（光晕）/ 默认（实心圆）三种样式
// ========================================================================
void OverlayScene::PaintCursor(Painter& painter,Point center,const SettingsModel& settings) {
// 获取光标相关设置
    const auto& cursor=settings.values["cursor"];
// 光标半径
    const float radius=cursor["size"].get<float>()*.5f;
// 光标不透明度
    const float opacity=cursor["opacity"].get<float>();
// 光标颜色
    auto color=Hex(cursor["color"].get<std::string>(),opacity);
// 光晕颜色（降低透明度）
    auto glow=color; glow.a*=cursor["glow"].get<float>();
// 绘制光晕
    painter.Glow(center,radius*1.9f,glow);
// 根据样式选择绘制方式
    const auto style=cursor["style"].get<std::string>();
// ring 样式：圆环
    if (style=="ring") painter.Ring(center,radius,cursor["border_width"].get<float>(),color);
// halo 样式：半透明光晕 + 中心实心圆
    else if (style=="halo") { color.a*=.38f; painter.Glow(center,radius*1.3f,color); painter.Circle(center,radius*.7f,color); }
// 默认：半透明实心圆
    else { color.a*=.24f; painter.Circle(center,radius,color); }
}
// ========================================================================
// Paint — 绘制当前帧完整场景
// 参数: bounds=显示器边界, dpi=DPI, inputMonitor=是否显示按键面板,
//       now=当前时间, settings=用户设置
// 返回: DrawList 绘制命令列表
// ========================================================================
DrawList OverlayScene::Paint(Rect bounds,float dpi,bool inputMonitor,double now,const SettingsModel& settings) {
// 计算逻辑坐标尺寸（除以 DPI）
    const float width=bounds.width/dpi,height=bounds.height/dpi;
// 创建 2D 绘制器
    Painter painter(width,height);
// 坐标转换 lambda：桌面物理坐标 -> 本显示器逻辑坐标
    const auto local=[&](Point p) { return Point{(p.x-bounds.x)/dpi,(p.y-bounds.y)/dpi}; };
// 转换光标位置到本地坐标
    const auto cursor=local(m_cursor);
// 绘制光标指示（如启用）
    if (settings.values["cursor"]["enabled"].get<bool>()) PaintCursor(painter,cursor,settings);
// 绘制所有点击涟漪
    for(const auto& ripple:m_ripples) {
// 计算涟漪动画进度 t∈[0,1]
        const float t=static_cast<float>((now-ripple.born)/.32);
// 透明度随进度衰减
        auto color=ripple.color; color.a=1-Smooth(t);
// 涟漪圆环：半径随时间扩散，透明度递减
        painter.Ring(local(ripple.position),11+16*(1-std::pow(1-t,3.f)),2,color);
    }
// 获取激光设置
    const auto& laser=settings.values["laser"];
// 激光颜色
    auto laserColor=Hex(laser["color"].get<std::string>());
// 激光线宽与光晕强度
    const float lineWidth=laser["width"].get<float>(), glow=laser["glow"].get<float>();
// 激光寿命
    const double life=laser["fade_ms"].get<double>()/1000;
// 绘制激光轨迹（遍历相邻点对）
    for(size_t i=1;i<m_laser.size();++i) {
// 跳过不同笔画之间的连接
        if(m_laser[i-1].stroke!=m_laser[i].stroke) continue;
// 转换两点到本地坐标
        auto a=local(m_laser[i-1].position),b=local(m_laser[i].position);
// 距离过近则跳过
        if(std::hypot(a.x-b.x,a.y-b.y)<.01f) continue;
// 计算透明度（保持一段时间后淡出）
        const float alpha=Fade(now-m_laser[i-1].born,life*.55,life*.45);
// 二次贝塞尔中点插值：用圆头线原语实现平滑连接
        // Quadratic midpoint interpolation with round joins/caps from the line primitive.
// 获取前一个点（用于贝塞尔控制）
        const auto before=i>1&&m_laser[i-2].stroke==m_laser[i].stroke?local(m_laser[i-2].position):a;
// 计算线段中点作为贝塞尔控制点
        const auto start=Mix(before,a,.5f),end=Mix(a,b,.5f);
// 步进插值细分曲线（2 段近似贝塞尔）
        auto previous=start;
        for(unsigned step=1;step<=2;++step) {
// 计算插值参数 t
            const float t=step*.5f;
// 二次贝塞尔插值计算下一点
            const auto next=Mix(Mix(start,a,t),Mix(a,end,t),t);
// 绘制外层发光线（3 倍线宽，低透明度）
            auto outer=laserColor; outer.a=alpha*glow*.25f;
            painter.Line(previous,next,lineWidth*3,outer);
// 绘制核心线（正常线宽，高透明度）
            auto core=laserColor; core.a=alpha*.95f;
            painter.Line(previous,next,lineWidth,core);
            previous=next;
        }
// 最后一段：直接连接到实际光标位置
        if(i+1==m_laser.size()) { auto core=laserColor;core.a=alpha;painter.Line(end,b,lineWidth,core); }
    }
// 激光按住时绘制光标处激光点
    if(m_laserHeld) {
// 外层光晕
        auto outer=laserColor;outer.a=glow;
// 中圆
        painter.Glow(cursor,lineWidth*5,outer);
// 中心亮点
        outer.a=.7f;painter.Circle(cursor,lineWidth*1.2f,outer);
        painter.Circle(cursor,lineWidth*.5f,{1,.96f,.9f,1});
    }
// 获取绘图标注颜色与线宽
    auto boxColor=Hex(settings.values["drawing"]["color"].get<std::string>());
    const float boxWidth=settings.values["drawing"]["width"].get<float>();
// 绘制所有绘图框/标注
    for(const auto& box:m_boxes){
// 转换框起点终点到本地坐标
        const auto a=local(box.start),b=local(box.end);
// 调用标注绘制函数
        PaintAnnotation(painter,a,b,box.shape,boxWidth,boxColor);
    }
// 非输入监视模式（仅画光标/特效）则直接返回
    if(!inputMonitor) return painter.List();
// ===== 按键监听面板绘制 =====
    const auto& config=settings.values["input"];
// 获取输入显示设置：缩放与不透明度
    const float scale=config["scale"].get<float>(),opacity=config["opacity"].get<float>();

// 获取偏移量与保持/淡出时间
    const float x=config["offset_x"].get<float>(),y=config["offset_y"].get<float>();
    const double hold=config["hold_ms"].get<double>()/1000,fade=config["fade_ms"].get<double>()/1000;
// 获取强调色
    auto accent=Hex(settings.values["general"]["accent"].get<std::string>());
// 鼠标可见度：按下时完全可见，释放后渐隐
    const float mouseVisible=m_input.AnyMouse()?1:Fade(now-m_input.mouseReleased,hold,fade);
// 滚轮指示器：滚轮事件后短暂显示
    const float wheel=config["show_wheel"].get<bool>()?Fade(now-m_input.wheelTime,.45,.2):0;
// 综合鼠标面板透明度
    const float mouseAlpha=config["mouse_enabled"].get<bool>()?std::max(mouseVisible,wheel)*opacity:0;
// 统计可见行数（用于自动定位）
    size_t visibleRows=0;
// excluded lambda：判断和弦是否在排除列表中
    const auto excluded=[&](const input::Chord& chord){
        for(const auto& key:settings.values["exclusions"]["hotkeys"])
            if(chord.combination==input::ParseHotkey(key.get_ref<const std::string&>()))return true;
        return false;
    };
// 统计可见和弦行数
    if(config["keyboard_enabled"].get<bool>()) for(const auto& chord:m_input.Chords())
        if(!excluded(chord)&&(chord.released<0||Fade(now-chord.released,hold,fade)>0))++visibleRows;
// 计算面板总高度（每行 42px + 鼠标面板 64px）
    const float groupHeight=static_cast<float>(visibleRows)*42*scale+(mouseAlpha>0?64*scale:0);
// 面板与光标之间的间距
    const float gap=std::max(16.f,y);
// 默认在光标下方
    float groupY=cursor.y+gap;
// 下方空间不足时翻转到上方
    if(groupY+groupHeight>height-8)groupY=cursor.y-gap-groupHeight;
// 钳位到屏幕安全区域
    groupY=std::clamp(groupY,8.f,std::max(8.f,height-groupHeight-8));
// followOrigin lambda：计算第 rowOffset 行的起始 x 坐标（水平跟随光标居中）
    auto followOrigin=[&](float itemWidth,float rowOffset){
        return Point{std::clamp(cursor.x+x-itemWidth*.5f,8.f,std::max(8.f,width-itemWidth-8)),groupY+rowOffset};
    };
// 当前绘制行号
    float row=0;
// 遍历所有按键和弦（从最新到最旧）
    if(config["keyboard_enabled"].get<bool>()) for(auto item=m_input.Chords().rbegin();item!=m_input.Chords().rend();++item) {
// 跳过排除的和弦
        if(excluded(*item))continue;
// 计算该行透明度
        const float alpha=opacity*(item->released<0?1:Fade(now-item->released,hold,fade));
// 透明度过低则跳过
        if(alpha<=0) continue;
// 构建按键标签列表
        std::vector<std::string> labels;
// 获取和弦组合
        const auto& combo=item->combination;
// 添加修饰键标签（Ctrl/Shift/Alt/Win）
        if(combo.ctrl)labels.emplace_back("Ctrl");if(combo.shift)labels.emplace_back("Shift");
        if(combo.alt)labels.emplace_back("Alt");if(combo.win)labels.emplace_back("Win");
// 添加主键标签
        labels.push_back(input::Label(combo.primary));
// 排版所有标签文字
        std::vector<text::Layout> text;
// 计算总宽度
        float total=0;
// 排版每个标签并累加总宽度
        for(const auto& label:labels) {text.push_back(m_text.Shape(label,12*scale,dpi));total+=std::max(34*scale,text.back().width+20*scale)+6*scale;}
// 修正最后一个元素的间距
        total-=6*scale;
// 获取本行起始位置
        auto origin=followOrigin(total,row*42*scale);
// 逐个绘制按键圆角矩形与文字
        for(size_t i=0;i<labels.size();++i) {
// 计算按键宽度（最小 34px）
            const float keyWidth=std::max(34*scale,text[i].width+20*scale);
// 绘制深色按键背景
            painter.RoundedRect({origin.x,origin.y,keyWidth,34*scale},7*scale,{.09f,.105f,.13f,alpha});
// 绘制按键文字（居中）
            painter.Text(text[i],{origin.x+(keyWidth-text[i].width)*.5f,origin.y+(34*scale-text[i].height)*.5f},{.95f,.97f,1,alpha});
// 移动到下一个按键位置
            origin.x+=keyWidth+6*scale;
        }
// 行号递增
        ++row;
    }
// ===== 鼠标状态面板绘制 =====
    if(config["mouse_enabled"].get<bool>()) {
// 鼠标面板透明度
        const float alpha=mouseAlpha;
// 透明度大于零时绘制
        if(alpha>0) {
// 获取鼠标面板起始位置
            auto origin=followOrigin(48*scale,row*42*scale);
// 绘制鼠标外形圆角矩形
            painter.RoundedRect({origin.x,origin.y,48*scale,64*scale},16*scale,{.09f,.105f,.13f,alpha});
// 高亮色
            auto highlight=accent;highlight.a=alpha;
// 左键按下高亮
            if(m_input.Down(input::Key::MouseLeft))painter.RoundedRect({origin.x+4*scale,origin.y+4*scale,18*scale,26*scale},6*scale,highlight);
// 右键按下高亮
            if(m_input.Down(input::Key::MouseRight))painter.RoundedRect({origin.x+26*scale,origin.y+4*scale,18*scale,26*scale},6*scale,highlight);
// 中键（滚轮）高亮
            auto scroll=highlight;scroll.a*=m_input.Down(input::Key::MouseMiddle)?1:std::max(.35f,wheel);
// 绘制滚轮条
            painter.RoundedRect({origin.x+21*scale,origin.y+9*scale,6*scale,15*scale},3*scale,scroll);
// 滚轮指示箭头（几何绘制，非字体字形）
            if(wheel>0) {
                // Geometry, not a font glyph: both directions share the same centered bounds.
// 滚轮透明度
                const float wheelAlpha=wheel*opacity;
// 滚轮徽章背景
                auto badge=accent;badge.a=wheelAlpha*.28f;
// 徽章中心
                painter.RoundedRect({origin.x+10*scale,origin.y+31*scale,28*scale,28*scale},8*scale,badge);
// 滚轮方向（向上为负）
                const Point center{origin.x+24*scale,origin.y+45*scale};
// 箭头尖端
                const float direction=m_input.wheelDirection>0?-1.f:1.f;
// 箭头尾部
                const Point tip{center.x,center.y+direction*7*scale};
// 箭头颜色
                const Point tail{center.x,center.y-direction*7*scale};
// 绘制箭头主干
                const Color ink{.98f,.99f,1.f,wheelAlpha};
// 绘制箭头左翼
                painter.Line(tail,tip,3*scale,ink);
// 绘制箭头右翼
                painter.Line({center.x-6*scale,tip.y-direction*6*scale},tip,3*scale,ink);
                painter.Line({center.x+6*scale,tip.y-direction*6*scale},tip,3*scale,ink);
            }
// 绘制侧键（Mouse4/Mouse5）
            if(config["show_side_buttons"].get<bool>()) {
                if(m_input.Down(input::Key::Mouse4))painter.RoundedRect({origin.x-4*scale,origin.y+32*scale,5*scale,10*scale},2*scale,highlight);
                if(m_input.Down(input::Key::Mouse5))painter.RoundedRect({origin.x-4*scale,origin.y+46*scale,5*scale,10*scale},2*scale,highlight);
            }
        }
    }
    return painter.List();
}
// ========================================================================
// PaintAnnotation — 绘制屏幕标注（静态方法）
// 支持 arrow（箭头）/ ellipse（椭圆）/ 默认（矩形）三种形状
// ========================================================================
void OverlayScene::PaintAnnotation(Painter& painter,Point a,Point b,std::string_view shape,float stroke,Color color){
// 计算边界框
    const float left=std::min(a.x,b.x),top=std::min(a.y,b.y),right=std::max(a.x,b.x),bottom=std::max(a.y,b.y);
// --- 箭头形状 ---
    if(shape=="arrow"){
// 计算长度，太短则不绘制
        const float length=std::hypot(b.x-a.x,b.y-a.y);if(length<2)return;
// 计算方向向量与箭头头部大小
        const float ux=(b.x-a.x)/length,uy=(b.y-a.y)/length,head=std::min(length*.4f,12.f+stroke*2);
// 绘制主线
        painter.Line(a,b,stroke,color);
// 绘制箭头左翼
        painter.Line(b,{b.x-ux*head-uy*head*.55f,b.y-uy*head+ux*head*.55f},stroke,color);
// 绘制箭头右翼
        painter.Line(b,{b.x-ux*head+uy*head*.55f,b.y-uy*head-ux*head*.55f},stroke,color);return;
    }
// 边界太小则跳过
    if(right-left<1||bottom-top<1)return;
// --- 椭圆形状 ---
    if(shape=="ellipse"){
// 计算中心与半径
        const Point center{(left+right)*.5f,(top+bottom)*.5f};const float rx=(right-left)*.5f,ry=(bottom-top)*.5f;
// 用 48 段线段逼近椭圆
        Point previous{right,center.y};
        for(int n=1;n<=48;++n){float angle=n*6.2831853f/48;Point next{center.x+rx*std::cos(angle),center.y+ry*std::sin(angle)};painter.Line(previous,next,stroke,color);previous=next;}
// --- 矩形形状（默认） ---
    }else{
// 半透明填充
        auto fill=color;fill.a=.06f;painter.FillRect({left,top,right-left,bottom-top},fill);
// 绘制四条边框线
        painter.Line({left,top},{right,top},stroke,color);painter.Line({right,top},{right,bottom},stroke,color);
        painter.Line({right,bottom},{left,bottom},stroke,color);painter.Line({left,bottom},{left,top},stroke,color);
    }
// ih::features 命名空间结束
}
}


