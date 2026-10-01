import type * as React from 'react';

type Style = React.CSSProperties;
/** Visual weight tier: 1 (or omitted) main — always shown; 2 secondary — hidden below 480px; 3 tertiary — hidden below 720px. */
export type Priority = 1 | 2 | 3;
export type Density = 'compact' | 'comfortable' | 'spacious';

/* Foundations */
export type IconName =
  | 'play' | 'pause' | 'stop' | 'record' | 'skip-start' | 'skip-end' | 'step-back' | 'step-forward' | 'loop'
  | 'cube' | 'group' | 'camera' | 'light' | 'eye' | 'eye-off' | 'lock' | 'unlock'
  | 'select' | 'move' | 'rotate' | 'scale' | 'hand' | 'snap' | 'grid' | 'zoom-fit'
  | 'folder' | 'document' | 'image' | 'film' | 'video-clip' | 'audio' | 'speaker' | 'speaker-mute'
  | 'save' | 'import' | 'export' | 'render'
  | 'undo' | 'redo' | 'cut' | 'link' | 'add' | 'subtract' | 'dismiss' | 'checkmark' | 'search' | 'filter'
  | 'settings' | 'options' | 'more' | 'chevron-right' | 'chevron-down' | 'pin' | 'keyframe' | 'keyframe-filled'
  | 'layers' | 'maximize' | 'fullscreen-exit' | 'square' | 'restore' | 'panel-left' | 'panel-right' | 'panel-bottom' | 'drag' | 'retry'
  | 'server' | 'gauge' | 'clock' | 'timer' | 'terminal'
  | 'window-new' | 'window-dock' | 'windows' | 'monitor' | 'monitor-move' | 'output'
  | 'eyedropper' | 'pin-filled' | 'sort-up' | 'sort-down' | 'folder-open' | 'reset' | 'history' | 'keyboard'
  | 'color' | 'curve' | 'table' | 'copy' | 'paste' | 'delete'
  | 'list' | 'calendar' | 'font' | 'bell' | 'chevron-left' | 'chevron-up' | 'zoom-in' | 'zoom-out' | 'person' | 'gpu'
  | 'split-right' | 'split-down' | 'home' | 'arrow-up' | 'arrow-down' | 'circle' | 'enter' | 'wrap' | 'mic' | 'headphones'
  | 'expand' | 'collapse' | 'bug' | 'help' | 'histogram' | 'plot' | 'task' | 'layout'
  | 'extension' | 'globe' | 'policy' | 'sync' | 'laptop' | 'team' | 'brush' | 'database'
  | 'info' | 'success' | 'warning' | 'error';
export interface IconProps { name: IconName; size?: 12 | 16 | number; title?: string; className?: string; style?: Style }
export declare function Icon(props: IconProps): React.ReactElement;

/* Actions */
export interface ButtonProps extends React.ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: 'primary' | 'secondary' | 'subtle' | 'danger';
  size?: 'sm' | 'md' | 'lg';
  icon?: IconName;
  iconAfter?: IconName;
}
export declare function Button(props: ButtonProps): React.ReactElement;

export interface IconButtonProps extends Omit<React.ButtonHTMLAttributes<HTMLButtonElement>, 'children'> {
  icon: IconName;
  /** Accessible name and tooltip. Required. */
  label: string;
  shortcut?: string;
  /** Toggle state; omit for one-shot actions. */
  pressed?: boolean;
  size?: 'sm' | 'md';
  tone?: 'danger';
}
export declare function IconButton(props: IconButtonProps): React.ReactElement;

export interface SegmentedOption { value: string; label: string; icon?: IconName; shortcut?: string; showLabel?: boolean }
export interface SegmentedControlProps {
  options: SegmentedOption[];
  value?: string; defaultValue?: string; onChange?: (value: string) => void;
  label?: string; tone?: 'neutral' | 'accent'; size?: 'sm' | 'md'; className?: string;
}
export declare function SegmentedControl(props: SegmentedControlProps): React.ReactElement;

export interface ToolbarProps {
  variant?: 'main' | 'panel' | 'floating'; label?: string; children?: React.ReactNode; className?: string; style?: Style;
  /** Menu items that repeat everything with priority 2–3; shown behind a "…" button when the bar is narrower than 720px. */
  overflow?: MenuItem[]; overflowLabel?: string;
}
export declare function Toolbar(props: ToolbarProps): React.ReactElement;
export declare namespace Toolbar {
  function Separator(props?: { priority?: Priority }): React.ReactElement;
  function Spacer(): React.ReactElement;
  function Label(props: { children?: React.ReactNode }): React.ReactElement;
  /** A group of controls that hides as one unit in a narrow bar. */
  function Group(props: { priority?: Priority; label?: string; children?: React.ReactNode; className?: string }): React.ReactElement;
}

/* Inputs */
export interface TextFieldProps extends Omit<React.InputHTMLAttributes<HTMLInputElement>, 'size'> {
  icon?: IconName; suffix?: React.ReactNode; invalid?: boolean; size?: 'sm' | 'md';
}
export declare function TextField(props: TextFieldProps): React.ReactElement;

export interface NumberFieldProps {
  value?: number; defaultValue?: number; onChange?: (value: number) => void;
  precision?: number; step?: number; min?: number; max?: number;
  unit?: string; label?: string; axis?: 'x' | 'y' | 'z';
  /** Field-slider: with min and max, the field fills to show the share and dragging across the field covers the whole range. One field instead of Slider + field. */
  bar?: boolean;
  /** Several selected objects have different values: shows "—"; a typed value goes to all of them. */
  mixed?: boolean;
  /** Spin Box: ▲▼ buttons on the right for counters and small integers (with precision — Double Spin Box). Anvil.SpinBox is an alias. */
  spin?: boolean;
  size?: 'sm' | 'md'; disabled?: boolean; 'aria-label'?: string; className?: string; style?: Style;
}
/* Typed values may be arithmetic: 2*pi, 1920/2, (4+2)^2 — evaluated without eval(); a trailing unit is ignored. */
export declare function NumberField(props: NumberFieldProps): React.ReactElement;

export interface Vector3FieldProps {
  value?: [number, number, number]; defaultValue?: [number, number, number]; onChange?: (value: [number, number, number]) => void;
  unit?: string; precision?: number; step?: number; label?: string; size?: 'sm' | 'md'; disabled?: boolean; className?: string; style?: Style;
  /** Multi-selection: true, or per axis [x, y, z] — those axes show "—". */
  mixed?: boolean | [boolean, boolean, boolean];
}
export declare function Vector3Field(props: Vector3FieldProps): React.ReactElement;

export interface SliderProps {
  value?: number; defaultValue?: number; onChange?: (value: number) => void;
  min?: number; max?: number; step?: number; precision?: number; unit?: string;
  format?: (value: number) => string; showValue?: boolean; label?: string; disabled?: boolean; className?: string; style?: Style;
}
export declare function Slider(props: SliderProps): React.ReactElement;

export interface CheckboxProps {
  checked?: boolean; defaultChecked?: boolean; onChange?: (checked: boolean) => void;
  indeterminate?: boolean; disabled?: boolean; children?: React.ReactNode; 'aria-label'?: string; className?: string;
}
export declare function Checkbox(props: CheckboxProps): React.ReactElement;

export interface ToggleProps {
  checked?: boolean; defaultChecked?: boolean; onChange?: (checked: boolean) => void;
  disabled?: boolean; children?: React.ReactNode; 'aria-label'?: string; className?: string;
}
export declare function Toggle(props: ToggleProps): React.ReactElement;

export interface SelectOption { value: string; label: string; disabled?: boolean }
export interface SelectProps extends Omit<React.SelectHTMLAttributes<HTMLSelectElement>, 'size'> {
  options: Array<string | SelectOption>; size?: 'sm' | 'md';
}
export declare function Select(props: SelectProps): React.ReactElement;

/* Layout */
export interface TitleBarProps {
  /** 'tool': title bar of a secondary window (ToolWindow) — panel name + document instead of the app menu, always 32px. */
  variant?: 'main' | 'tool';
  /** The window has no focus: the whole bar steps down to text-tertiary, as system title bars do. */
  inactive?: boolean;
  /** variant 'tool': icon before the name and the document name after it. */
  icon?: IconName; document?: React.ReactNode;
  /** Where the window buttons go and who draws them. macOS: the OS draws them in an 80px inset; app menus live in the system menu bar. */
  platform?: 'windows' | 'macos' | 'linux';
  /** Menu names, or full menus with items (rendered by MenuBar). */
  menus?: Array<string | MenuBarMenu>; onMenuSelect?: (item: MenuItem, menu: MenuBarMenu) => void;
  title?: React.ReactNode; appName?: string; activeMenu?: string;
  /** Maximized window: the maximize button becomes "restore". */
  maximized?: boolean;
  /** F11 / Ctrl+Cmd+F: no window buttons, an "exit full screen" button instead. */
  fullscreen?: boolean;
  /** Linux only: window buttons on the left when the desktop is set up that way. */
  controlsSide?: 'left' | 'right';
  /** macOS: also show the menus in the title bar. */
  inAppMenu?: boolean;
  /** false when the OS or shell draws the window buttons (titleBarOverlay, hiddenInset, server-side decorations). */
  windowControls?: boolean;
  /** Mockups only: neutral dots in the macOS inset. */
  captionPlaceholder?: boolean;
  onMinimize?: () => void; onToggleMaximize?: () => void; onClose?: () => void; onExitFullscreen?: () => void;
  children?: React.ReactNode; className?: string;
}
export declare function TitleBar(props: TitleBarProps): React.ReactElement;

export interface TabItem { id: string; label: string; icon?: IconName; count?: number; dirty?: boolean; title?: string }
export interface TabsProps {
  items: Array<string | TabItem>; value?: string; defaultValue?: string; onChange?: (id: string) => void;
  variant?: 'panel' | 'document'; onClose?: (id: string) => void; label?: string; className?: string; style?: Style;
}
export declare function Tabs(props: TabsProps): React.ReactElement;

export interface PanelProps {
  title?: React.ReactNode; icon?: IconName;
  tabs?: Array<string | TabItem>; activeTab?: string; defaultTab?: string; onTabChange?: (id: string) => void;
  actions?: React.ReactNode; toolbar?: React.ReactNode; focused?: boolean; padded?: boolean;
  /** The panel's focal point: the selected object / file / preset. */
  subject?: { icon?: IconName; title: React.ReactNode; meta?: React.ReactNode; aside?: React.ReactNode };
  density?: Density;
  children?: React.ReactNode; className?: string; style?: Style; bodyStyle?: Style;
}
export interface WorkspaceProps {
  titleBar?: React.ReactNode; toolbar?: React.ReactNode; statusBar?: React.ReactNode;
  left?: React.ReactNode; center: React.ReactNode; right?: React.ReactNode; bottom?: React.ReactNode;
  /** CSS lengths, e.g. '256px'. */
  leftWidth?: string; rightWidth?: string; bottomHeight?: string;
  leftIcon?: IconName; leftLabel?: string; rightIcon?: IconName; rightLabel?: string;
  defaultOpen?: 'left' | 'right'; density?: Density; className?: string; style?: Style;
  /** One window spanned over several monitors (NVIDIA Surround, AMD Eyefinity): x of the two bezels in px from the window's left edge.
   * The column gutters sit on the bezels (left column = monitor 1, centre = monitor 2, right column = monitor 3) and the main toolbar tools stay on the middle monitor. */
  seams?: [number, number];
}
/** Adaptive window frame that fills the window (100dvh). Omit left / right when those panels live in another window (multi-monitor): the column collapses and the centre takes the room. Width: below 1100px the left panel folds into a rail + drawer, below 760px the right one too. Height: below 760px the chrome bars go compact, below 600px the bottom panel collapses to its tab strip. Shape: narrower than 3:2 (1:1, 5:4, 4:3) the bottom panel spans all columns (up to 34%, max 600px); from 2:1 (21:9, 32:9) the side columns run full height, the bottom panel sits under the center only and the main toolbar tools sit in a centered band (size-toolbar-band). */
export declare function Workspace(props: WorkspaceProps): React.ReactElement;
export declare function Panel(props: PanelProps): React.ReactElement;

export interface StackPanel {
  id: string; title: string; icon?: IconName; count?: number; content: React.ReactNode;
  actions?: React.ReactNode; subject?: PanelProps['subject']; toolbar?: React.ReactNode;
  density?: Density; focused?: boolean; padded?: boolean;
  /** Content fills the pane height (viewport, timeline, curves). */
  fill?: boolean;
}
export interface PanelStackProps {
  /** Most important first — it is always visible. The set never depends on screen size. */
  panels: StackPanel[];
  direction?: 'auto' | 'horizontal' | 'vertical' | 'grid';
  minPaneWidth?: number; minPaneHeight?: number; maxPanes?: number;
  /** direction 'grid': preferred pane shape (width / height). The grid takes the most panes that fit, then no empty cells, then panes closest to this shape. Default 1.6 (16:10) for viewports; use 1 for panels in a panel window (ToolWindow). */
  paneAspect?: number;
  className?: string; style?: Style;
}
/** Shows as many panels side by side / stacked as fit its own size; the rest fold into tabs of the last pane. */
export declare function PanelStack(props: PanelStackProps): React.ReactElement;

export interface PropertyGridProps {
  labelWidth?: string;
  /** Search: rows whose label (or keywords) contain it — in every group, advanced rows included; matches are highlighted and counted in the group header. A group whose title matches shows all its rows. */
  query?: string;
  /** 'modified' — rows that differ from the default; 'animated' — rows with keys. */
  filter?: 'all' | 'modified' | 'animated';
  /** Shown as "Показати всі" when nothing matches. */
  onClearQuery?: () => void;
  children?: React.ReactNode; className?: string; style?: Style;
}
export interface PropertyGroupProps {
  title: React.ReactNode; icon?: IconName; aside?: React.ReactNode;
  /** Shown in the header while the group is collapsed: "Path tracing · 256 семплів · денойзер". */
  summary?: React.ReactNode;
  /** The "Закріплені" group at the top. */
  pinned?: boolean;
  /** Start with advanced rows shown (normally behind "Ще N параметрів"). */
  defaultShowAdvanced?: boolean;
  open?: boolean; defaultOpen?: boolean; onToggle?: (open: boolean) => void; children?: React.ReactNode;
}
export interface PropertyRowProps {
  label: React.ReactNode; children?: React.ReactNode; hint?: string;
  modified?: boolean; disabled?: boolean;
  keyframe?: 'none' | 'animated' | 'key'; onKeyframe?: () => void; className?: string;
  /** Rarely used: hidden behind "Ще N параметрів" until expanded — search and filters still find it. */
  advanced?: boolean;
  /** Pin button in the left gutter (on hover; always visible when pinned). */
  pinned?: boolean; onPin?: () => void;
  /** Extra search words (English names, abbreviations): "clearcoat", "sss". */
  keywords?: string;
  /** Row context menu: reset to default, copy / paste value, key, pin, copy script path. */
  onContextMenu?: (e: React.MouseEvent) => void;
}
export declare function PropertyGrid(props: PropertyGridProps): React.ReactElement;
export declare namespace PropertyGrid {
  function Group(props: PropertyGroupProps): React.ReactElement;
  function Row(props: PropertyRowProps): React.ReactElement;
}

export interface TreeNode {
  id: string; label: string; icon?: IconName; meta?: string;
  children?: TreeNode[]; expanded?: boolean; visible?: boolean; locked?: boolean;
}
export interface TreeViewProps {
  items: TreeNode[]; selected?: string[]; defaultSelected?: string[]; onSelect?: (ids: string[]) => void;
  focused?: boolean; toggles?: boolean; label?: string; className?: string; style?: Style;
}
export declare function TreeView(props: TreeViewProps): React.ReactElement;

export type StatusItem = '|' | { icon?: IconName; tone?: 'success' | 'warning' | 'danger' | 'accent'; label?: React.ReactNode; value?: React.ReactNode; title?: string; priority?: Priority };
export interface StatusBarProps { left?: StatusItem[]; right?: StatusItem[]; className?: string; style?: Style }
export declare function StatusBar(props: StatusBarProps): React.ReactElement;

/* Media */
export interface ViewportProps {
  kind?: '3d' | 'video'; label?: string;
  /** [label, value, priority?] — priority 3 hides below 720px, 2 below 480px of viewport width. */
  stats?: Array<[string, React.ReactNode] | [string, React.ReactNode, Priority]>;
  /** 3D grid: perspective floor or orthographic (top / front views). */
  view?: 'persp' | 'ortho';
  toolbar?: React.ReactNode; gizmo?: boolean; aspect?: number; frameLabel?: string;
  children?: React.ReactNode; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function Viewport(props: ViewportProps): React.ReactElement;

export interface TimelineClip { start: number; end: number; label: string; selected?: boolean }
export interface TimelineTrack {
  id?: string; name: string; kind?: 'video' | 'audio' | 'fx' | 'keys';
  muted?: boolean; locked?: boolean; clips?: TimelineClip[]; keys?: Array<number | { frame: number; selected?: boolean }>;
}
export interface TimelineProps {
  fps?: number; start?: number; end?: number; pxPerFrame?: number;
  current?: number; defaultCurrent?: number; onSeek?: (frame: number) => void;
  inPoint?: number; outPoint?: number; tracks: TimelineTrack[]; label?: string; className?: string; style?: Style;
}
export declare function Timeline(props: TimelineProps): React.ReactElement;

export interface TransportBarProps {
  fps?: number; duration?: number;
  frame?: number; defaultFrame?: number; onFrameChange?: (frame: number) => void;
  playing?: boolean; defaultPlaying?: boolean; onPlayingChange?: (playing: boolean) => void;
  loop?: boolean; onLoopChange?: (loop: boolean) => void;
  children?: React.ReactNode; className?: string; style?: Style;
}
export declare function TransportBar(props: TransportBarProps): React.ReactElement;

export interface ProgressBarProps {
  /** 0–1; omit for indeterminate. */
  value?: number; label?: React.ReactNode; showValue?: boolean; detail?: React.ReactNode;
  tone?: 'accent' | 'success' | 'warning' | 'danger' | 'neutral'; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function ProgressBar(props: ProgressBarProps): React.ReactElement;

export interface RenderJobProps {
  name: string; status?: 'queued' | 'running' | 'paused' | 'done' | 'failed';
  progress?: number; preset?: string; detail?: React.ReactNode; statusText?: string;
  selected?: boolean; actions?: React.ReactNode; className?: string; style?: Style;
}
export declare function RenderJob(props: RenderJobProps): React.ReactElement;

/* Feedback */
export interface BadgeProps {
  tone?: 'neutral' | 'accent' | 'info' | 'success' | 'warning' | 'danger';
  icon?: IconName; dot?: boolean; children?: React.ReactNode; className?: string; style?: Style;
}
export declare function Badge(props: BadgeProps): React.ReactElement;

export interface InfoBarProps {
  severity?: 'info' | 'success' | 'warning' | 'danger'; title?: React.ReactNode; children?: React.ReactNode;
  action?: React.ReactNode; onClose?: () => void; closable?: boolean;
  /** 'banner': full width of the window or document, under the title bar / above the content; no frame or radius. */
  layout?: 'inline' | 'banner';
  className?: string; style?: Style;
}
export declare function InfoBar(props: InfoBarProps): React.ReactElement;

export type MenuItem = '-' | { header: string } | {
  label: string; icon?: IconName; shortcut?: string; checked?: boolean;
  disabled?: boolean; danger?: boolean; active?: boolean; onSelect?: () => void;
  /** Submenu items (one level). */
  items?: MenuItem[];
  /** @deprecated use items. */
  submenu?: boolean;
};
export interface MenuProps {
  items: MenuItem[]; onSelect?: (item: MenuItem) => void; label?: string;
  /** Mockups: index of the item whose submenu is open. */
  defaultSubmenu?: number;
  /** Inside a submenu: ← returns to the parent. */
  onBack?: () => void;
  className?: string; style?: Style;
}
export declare function Menu(props: MenuProps): React.ReactElement;

export interface TooltipProps {
  content: React.ReactNode; shortcut?: string; placement?: 'bottom' | 'top' | 'left' | 'right';
  /** Rich tooltip: a heading plus up to three lines — e.g. why a disabled button is unavailable. No buttons or links. */
  heading?: React.ReactNode; rich?: boolean;
  delay?: number; open?: boolean; children: React.ReactNode;
}
export declare function Tooltip(props: TooltipProps): React.ReactElement;

/* Multi-monitor */
export interface ToolWindowProps {
  /** 'panels' (default): a window with panels moved out of the main window. 'output': a clean full-screen viewport / video for a reference monitor, no UI. */
  kind?: 'panels' | 'output';
  /** panels: window name ("Панелі", "Сцена"); output: what is shown ("Hero Cam"). */
  title?: React.ReactNode;
  /** panels: document name after the title. */
  document?: React.ReactNode;
  icon?: IconName;
  platform?: 'windows' | 'macos' | 'linux'; maximized?: boolean; controlsSide?: 'left' | 'right'; windowControls?: boolean; captionPlaceholder?: boolean;
  /** The window has no focus (only one Anvil window is active at a time). */
  inactive?: boolean;
  /** "Return to main window" button; closing the window does the same. dockable={false} hides it. */
  onDock?: () => void; dockable?: boolean; dockShortcut?: string;
  /** "Keep above the main window" toggle — for a panel window floating over the main one on a single monitor. Shown when pinned or onPin is given. */
  pinned?: boolean; onPin?: () => void;
  onMinimize?: () => void; onToggleMaximize?: () => void; onClose?: () => void;
  /** output: the hint chip ("Hero Cam · Монітор 2 · 1920 × 1080 · Esc закрити"). 'auto' (default) shows it for 2 s after the mouse moves. */
  hint?: 'auto' | 'always' | false; meta?: React.ReactNode;
  /** Usually a PanelStack direction="grid" paneAspect={1} (panels) or a Viewport (output). */
  children?: React.ReactNode;
  statusBar?: React.ReactNode; density?: Density; className?: string; style?: Style;
}
/** A secondary window on another monitor. Same panels and commands as in the main window — only their placement differs. Min size size-tool-window-min-width × -height. */
export declare function ToolWindow(props: ToolWindowProps): React.ReactElement;

export interface DockGuideProps {
  /** Where the dragged panel will land: a half of the pane under the cursor, 'center' as a tab, 'window' — outside every Anvil window (a new window). null — no target yet. */
  target?: 'left' | 'right' | 'top' | 'bottom' | 'center' | 'window' | null;
  /** The dragged panel following the cursor, in px of the overlay. */
  ghost?: { title: string; icon?: IconName; x: number; y: number };
  /** Size of the new window outline for target 'window'. Default [320, 240]. */
  windowSize?: [number, number];
  /** false hides the five-target cross (edge docking only). */
  cross?: boolean;
  className?: string; style?: Style;
}
/** Overlay shown while a panel tab is dragged: put it inside the pane (position: relative) under the cursor. Presentational — the app does the hit-testing. */
export declare function DockGuide(props: DockGuideProps): React.ReactElement;

/* ---------- More controls ---------- */
export interface ColorPickerProps {
  /** '#RRGGBB' (display sRGB). */
  value?: string; defaultValue?: string; onChange?: (hex: string) => void;
  /** false (default): a swatch + value field that opens the picker next to it — the inspector row. true: the picker itself, embedded in a panel or dialog. */
  inline?: boolean;
  /** Opacity 0–1: the field shows the checkerboard and "60 %"; omit for opaque colours. */
  alpha?: number; defaultAlpha?: number; onAlphaChange?: (a: number) => void;
  /** inline: show the opacity strip and field (the field form shows it when alpha is set). */
  showAlpha?: boolean;
  /** Field: 'float' shows 1.000 0.886 0.722 (engine-style) instead of HEX. */
  format?: 'hex' | 'float';
  /** Several selected objects have different colours: striped swatch and «Кілька значень»; a new colour goes to all. */
  mixed?: boolean;
  defaultMode?: 'rgb' | 'hsv';
  /** Eyedropper: the app samples the screen; `picking` shows the button pressed. */
  onPick?: () => void; picking?: boolean;
  /** Recent / document colours. */
  swatches?: string[];
  /** Colour-space note, e.g. "display sRGB" — values the scene stores as linear are converted by the app. */
  note?: string;
  /** Field: where the picker opens. */
  placement?: 'down' | 'up'; defaultOpen?: boolean;
  size?: 'sm' | 'md'; disabled?: boolean; 'aria-label'?: string; className?: string; style?: Style;
}
/** One colour control. Field by default (swatch + value, opens the picker); `inline` — saturation/value plane, hue and alpha strips, HEX, RGB or HSV field-sliders, before/after, recent colours. */
export declare function ColorPicker(props: ColorPickerProps): React.ReactElement;
export interface GradientStop { pos: number; color: string }
export interface GradientEditorProps {
  stops?: GradientStop[]; defaultStops?: GradientStop[]; onChange?: (stops: GradientStop[]) => void;
  interpolation?: 'linear' | 'smooth' | 'constant'; defaultInterpolation?: 'linear' | 'smooth' | 'constant'; onInterpolationChange?: (v: string) => void;
  defaultSelected?: number; pickerPlacement?: 'down' | 'up'; 'aria-label'?: string; className?: string; style?: Style;
}
/** Colour ramp: drag stops, arrows move the selected one, Delete removes it; position / colour / interpolation below. */
export declare function GradientEditor(props: GradientEditorProps): React.ReactElement;

export interface RangeSliderProps {
  value?: [number, number]; defaultValue?: [number, number]; onChange?: (v: [number, number]) => void;
  min?: number; max?: number; step?: number; precision?: number; unit?: string;
  /** Tick marks under the track. */
  marks?: number[];
  /** false: no number fields, the range is shown as text. */
  inputs?: boolean;
  label?: string; disabled?: boolean; className?: string; style?: Style;
}
/** Two thumbs on one track (frames in–out, LOD distances, levels) with a number field at each end. */
export declare function RangeSlider(props: RangeSliderProps): React.ReactElement;
export interface KnobProps {
  value?: number; defaultValue?: number; onChange?: (v: number) => void;
  min?: number; max?: number; step?: number; precision?: number; unit?: string;
  /** The arc grows from zero both ways (gain in dB, pan). */
  bipolar?: boolean;
  /** Double-click returns to it. Default: defaultValue. */
  resetValue?: number;
  format?: (v: number) => string; label: string; showLabel?: boolean;
  size?: 'sm' | 'md'; disabled?: boolean; className?: string; style?: Style;
}
/** Rotary control for audio / mixer values only: drag up/down, Shift fine, double-click resets. */
export declare function Knob(props: KnobProps): React.ReactElement;

export interface ChoiceOption { value: string; label: string; description?: string; disabled?: boolean }
export interface RadioGroupProps {
  options: ChoiceOption[]; value?: string; defaultValue?: string; onChange?: (v: string) => void;
  orientation?: 'vertical' | 'horizontal'; name?: string; label?: string; disabled?: boolean; className?: string; style?: Style;
}
export declare function RadioGroup(props: RadioGroupProps): React.ReactElement;
export interface ListOption { value: string; label: string; icon?: IconName; color?: string; meta?: React.ReactNode; group?: string; keywords?: string; disabled?: boolean }
export interface ComboboxProps {
  options: Array<string | ListOption>; value?: string; defaultValue?: string; onChange?: (v: string) => void;
  defaultQuery?: string; defaultOpen?: boolean; placeholder?: string;
  /** Under the list, e.g. "Створити матеріал…". */
  footer?: React.ReactNode;
  placement?: 'down' | 'up'; size?: 'sm' | 'md'; label?: string; disabled?: boolean; 'aria-label'?: string; className?: string; style?: Style;
}
/** Select with search: type to filter (label, meta, keywords), groups, ↑ ↓ Enter, match highlighted. For 15+ options. */
export declare function Combobox(props: ComboboxProps): React.ReactElement;
export interface TagInputProps {
  options?: Array<string | ListOption>; value?: string[]; defaultValue?: string[]; onChange?: (v: string[]) => void;
  /** Enter adds whatever was typed. */
  allowCustom?: boolean;
  defaultQuery?: string; defaultOpen?: boolean; placeholder?: string; label?: string; disabled?: boolean; 'aria-label'?: string; className?: string; style?: Style;
}
/** Several values as chips; Backspace removes the last one. */
export declare function TagInput(props: TagInputProps): React.ReactElement;

export interface TableColumn<R = any> {
  key: string; label: React.ReactNode;
  /** px or any CSS track; omitted = takes the rest. */
  width?: number | string;
  align?: 'start' | 'end'; mono?: boolean; secondary?: boolean; sortable?: boolean;
  render?: (row: R) => React.ReactNode; sortValue?: (row: R) => number | string;
}
export interface TableRow { id: string; icon?: IconName; muted?: boolean; children?: TableRow[]; [key: string]: any }
export interface DataTableProps {
  columns: TableColumn[]; rows: TableRow[];
  sort?: { key: string; dir: 'asc' | 'desc' } | null; defaultSort?: { key: string; dir: 'asc' | 'desc' } | null; onSortChange?: (s: { key: string; dir: 'asc' | 'desc' } | null) => void;
  selected?: string[]; defaultSelected?: string[]; onSelectionChange?: (ids: string[]) => void;
  /** Rows with children get a twisty in the first column. */
  tree?: boolean; defaultExpanded?: string[];
  onOpen?: (row: TableRow) => void; focused?: boolean; empty?: React.ReactNode; density?: Density; label?: string; className?: string; style?: Style;
}
/** Sortable, selectable table with a sticky header; numbers right-aligned in mono. */
export declare function DataTable(props: DataTableProps): React.ReactElement;

export interface PathFieldProps {
  value?: string; defaultValue?: string; onChange?: (path: string) => void;
  kind?: 'file' | 'folder';
  /** Shows a warning and "Знайти файл…" (onLocate). */
  missing?: boolean; onLocate?: () => void; onBrowse?: () => void;
  /** Short tag at the end: "EXR", "відносний". */
  badge?: React.ReactNode;
  /** Longer paths show the root and the last two parts: D:/…/paint/file.exr (full path on focus and in the tooltip). */
  maxChars?: number;
  placeholder?: string; size?: 'sm' | 'md'; label?: string; disabled?: boolean; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function PathField(props: PathFieldProps): React.ReactElement;
export interface AssetSlotProps {
  asset?: { name: string; meta?: React.ReactNode; icon?: IconName; preview?: string } | null;
  missing?: boolean;
  /** A matching file is dragged over the slot. */
  dragOver?: boolean; accept?: string;
  onBrowse?: () => void; onClear?: () => void; onLocate?: () => void;
  icon?: IconName; placeholder?: string; size?: 'sm' | 'md'; label?: string; 'aria-label'?: string; className?: string; style?: Style;
}
/** A reference to a project asset: thumbnail, name, metadata, drop target. */
export declare function AssetSlot(props: AssetSlotProps): React.ReactElement;
export interface CurveEditorProps {
  /** [x, y] in 0–1, sorted by x. */
  points?: Array<[number, number]>; defaultPoints?: Array<[number, number]>; onChange?: (pts: Array<[number, number]>) => void;
  defaultSelected?: number;
  /** End points keep x = 0 and x = 1. Default true. */
  lockEnds?: boolean;
  presets?: Array<{ label: string; points: Array<[number, number]> }>;
  height?: number; showValues?: boolean; label?: string; 'aria-label'?: string; className?: string; style?: Style;
}
/** Monotone curve through points (never overshoots): drag points, double-click adds, Delete removes, arrows nudge. */
export declare function CurveEditor(props: CurveEditorProps): React.ReactElement;

export interface DialogProps {
  open?: boolean; title: React.ReactNode; subtitle?: React.ReactNode; icon?: IconName;
  size?: 'sm' | 'md' | 'lg';
  onClose?: () => void; closable?: boolean;
  /** Tabs under the title (a Tabs element). */
  tabs?: React.ReactNode;
  /** Right side of the footer: buttons, primary last. Left side: summary text. */
  footer?: React.ReactNode; footerStart?: React.ReactNode;
  padded?: boolean; alert?: boolean;
  /** 'danger': irreversible action — danger icon and a red main button; focus goes to «Скасувати». */
  tone?: 'danger';
  /** Mockups: render in place instead of over the window. */
  inline?: boolean;
  density?: Density; children?: React.ReactNode; className?: string; style?: Style;
}
/** Modal over a scrim, centred in the window that opened it. Section headings inside: <h3 className="av-dialog-section">. */
export declare function Dialog(props: DialogProps): React.ReactElement | null;
export interface Command {
  id?: string; label: string; icon?: IconName;
  /** Where it lives in the menus: ['Рендер', 'Налаштування…']. */
  path?: string[];
  shortcut?: string;
  /** Current value for parameters: "256", "Увімк.". */
  value?: React.ReactNode;
  group?: string; keywords?: string; recent?: boolean; disabled?: boolean;
}
export interface CommandPaletteProps {
  items: Command[]; query?: string; defaultQuery?: string; onQueryChange?: (q: string) => void;
  onRun?: (c: Command) => void; onClose?: () => void;
  limit?: number; placeholder?: string; hint?: React.ReactNode; inline?: boolean; label?: string; className?: string; style?: Style;
}
/** Ctrl+Shift+P: every command, panel and parameter by name — with its shortcut, value and menu path. Empty query shows recent ones. */
export declare function CommandPalette(props: CommandPaletteProps): React.ReactElement;

export interface SplitButtonProps {
  children: React.ReactNode; icon?: IconName; variant?: 'primary' | 'secondary' | 'subtle'; size?: 'sm' | 'md';
  onClick?: () => void; shortcut?: string;
  items: MenuItem[]; onSelect?: (item: MenuItem) => void; menuLabel?: string;
  menuAlign?: 'start' | 'end'; placement?: 'down' | 'up'; defaultOpen?: boolean;
  disabled?: boolean; 'aria-label'?: string; className?: string; style?: Style;
}
/** The main action plus its variants behind the chevron: "Рендер ▾" → кадр / анімація / область. */
export declare function SplitButton(props: SplitButtonProps): React.ReactElement;
export interface Tool { value: string; icon: IconName; label: string; shortcut?: string }
export interface ToolFlyoutProps {
  tools: Tool[]; value?: string; defaultValue?: string; onChange?: (v: string) => void;
  /** The family's tool is the active one (pressed). */
  active?: boolean;
  onActivate?: (v: string) => void; label?: string; defaultOpen?: boolean; size?: 'sm' | 'md'; className?: string; style?: Style;
}
/** One toolbar slot per tool family: the last used tool on the face; hold, right-click or Alt+↓ for the rest. */
export declare function ToolFlyout(props: ToolFlyoutProps): React.ReactElement;

/* ===================== Modular windows ===================== */
export interface SplitterProps {
  /** 'vertical': between columns (drag left/right) · 'horizontal': between rows. */
  orientation?: 'vertical' | 'horizontal';
  onResizeStart?: () => void;
  /** delta in px since the drag started; total — px available to both sides. Keyboard: ±8, Shift ±32. */
  onResize?: (delta: number, total: number) => void;
  onResizeEnd?: () => void;
  /** Double click: equal sizes. */
  onReset?: () => void;
  value?: number; min?: number; max?: number; label?: string; 'aria-label'?: string; title?: string; className?: string; style?: Style;
}
export declare function Splitter(props: SplitterProps): React.ReactElement;

export type DockTree = DockSplitNode | DockTabsNode;
export interface DockSplitNode { type: 'split'; dir: 'row' | 'column'; sizes?: number[]; children: DockTree[] }
/** A tab group; `type` may be omitted. */
export interface DockTabsNode { type?: 'tabs'; id: string; panels: string[]; active?: string }
export type DockFormat = 'standard' | 'ultrawide' | 'boxy' | 'portrait';
export interface DockPanel {
  title: string; icon?: IconName; count?: number; content: React.ReactNode;
  actions?: React.ReactNode; toolbar?: React.ReactNode;
  subject?: { icon?: IconName; title: React.ReactNode; meta?: React.ReactNode; aside?: React.ReactNode };
  density?: Density; padded?: boolean;
  /** Content fills the panel edge to edge (viewport, timeline, log). */
  fill?: boolean;
}
export interface DockLayoutProps {
  panels: Record<string, DockPanel>;
  layout?: DockTree; defaultLayout?: DockTree; onLayoutChange?: (tree: DockTree, format: DockFormat) => void;
  /** A saved layout per screen format; the one for the current format wins over layout. */
  variants?: Partial<Record<DockFormat, DockTree>>;
  /** Force a format; otherwise from the container's aspect ratio (<1 portrait, <1.5 boxy, ≥2 ultrawide). */
  format?: DockFormat;
  /** A tab dropped outside the window (or «Відкріпити у вікно», Ctrl+Shift+D): open it as a ToolWindow at x, y (screen px). */
  onDetach?: (panelId: string, at: { x: number; y: number }) => void;
  onClosePanel?: (panelId: string) => void;
  focusedPanel?: string;
  /** Minimum group size [width, height] before parts fold into tabs. Default [220, 140]. */
  minPane?: [number, number];
  gap?: number;
  /** No dragging or closing (kiosk, presentation); maximize still works. */
  locked?: boolean;
  /** Group id maximized to the whole layout (Ctrl+Space). */
  defaultMaximized?: string;
  empty?: React.ReactNode;
  /** Mockups only: freeze a drag in progress. */
  previewDrag?: { panel: string; over: string; target: 'left' | 'right' | 'top' | 'bottom' | 'center'; cx: number; cy: number };
  className?: string; style?: Style;
}
export declare function DockLayout(props: DockLayoutProps): React.ReactElement;
export declare namespace DockLayout {
  /** Drop empty groups, merge same-direction splits, collapse single children. */
  function normalize(tree: DockTree): DockTree | null;
  function panelsOf(tree: DockTree): string[];
}

/* ===================== Text, labels, feedback ===================== */
export interface LabelProps { children?: React.ReactNode; htmlFor?: string; id?: string; required?: boolean; optional?: boolean; hint?: React.ReactNode; disabled?: boolean; className?: string; style?: Style }
export declare function Label(props: LabelProps): React.ReactElement;
export interface HeadingProps { level?: 1 | 2 | 3 | 4; children?: React.ReactNode; aside?: React.ReactNode; id?: string; className?: string; style?: Style }
export declare function Heading(props: HeadingProps): React.ReactElement;
export type Tone = 'neutral' | 'error' | 'warning' | 'success';
export interface HelperTextProps { tone?: Tone; icon?: IconName; id?: string; children?: React.ReactNode; className?: string; style?: Style }
export declare function HelperText(props: HelperTextProps): React.ReactElement;
export interface FieldProps {
  label?: React.ReactNode; htmlFor?: string; required?: boolean; optional?: boolean; hint?: React.ReactNode;
  /** Neutral help under the field; replaced by error / warning / success. */
  help?: React.ReactNode; error?: React.ReactNode; warning?: React.ReactNode; success?: React.ReactNode;
  counter?: React.ReactNode; layout?: 'stack' | 'inline'; disabled?: boolean;
  /** The control: gets id, aria-describedby and invalid. */
  children: React.ReactElement; className?: string; style?: Style;
}
export declare function Field(props: FieldProps): React.ReactElement;

export interface PopoverProps {
  trigger: React.ReactElement; children?: React.ReactNode; title?: React.ReactNode; footer?: React.ReactNode;
  placement?: 'bottom' | 'top' | 'start' | 'end'; width?: number;
  open?: boolean; defaultOpen?: boolean; onOpenChange?: (open: boolean) => void; className?: string; style?: Style;
}
export declare function Popover(props: PopoverProps): React.ReactElement;
export interface TeachingTipProps {
  title: React.ReactNode; children?: React.ReactNode; target?: React.ReactNode; media?: React.ReactNode;
  step?: number; steps?: number; onNext?: () => void; onBack?: () => void; onClose?: () => void; doneLabel?: string;
  placement?: 'bottom' | 'top'; width?: number; open?: boolean; defaultOpen?: boolean; className?: string; style?: Style;
}
export declare function TeachingTip(props: TeachingTipProps): React.ReactElement | null;
export interface SpinnerProps { size?: 12 | 16 | 24 | 32; label?: React.ReactNode; block?: boolean; className?: string; style?: Style }
export declare function Spinner(props: SpinnerProps): React.ReactElement;
export type Status = 'ok' | 'running' | 'warning' | 'error' | 'offline' | 'idle' | 'live' | 'paused';
export interface StatusIndicatorProps { status?: Status; label?: React.ReactNode | false; pulse?: boolean; title?: string; className?: string; style?: Style }
export declare function StatusIndicator(props: StatusIndicatorProps): React.ReactElement;
export type ToastSeverity = 'info' | 'success' | 'warning' | 'error' | 'progress';
export interface ToastProps {
  id?: string | number; severity?: ToastSeverity; title: React.ReactNode; children?: React.ReactNode; action?: React.ReactNode;
  time?: string; count?: number; icon?: IconName;
  /** progress: 0–1. */
  value?: number;
  /** 0–1 of the auto-dismiss timer left (success / info only). */
  timeLeft?: number;
  onClose?: () => void; closable?: boolean; onPause?: () => void; onResume?: () => void; leaving?: boolean; className?: string; style?: Style;
}
export declare function Toast(props: ToastProps): React.ReactElement;
export interface ToastStackProps {
  toasts: ToastProps[]; position?: 'bottom-end' | 'top-end'; max?: number;
  onDismiss?: (id: string | number) => void; onShowAll?: () => void; inline?: boolean; className?: string; style?: Style;
}
export declare function ToastStack(props: ToastStackProps): React.ReactElement;
export interface NotificationItem { id: string | number; severity?: ToastSeverity; title: React.ReactNode; text?: React.ReactNode; time?: string; unread?: boolean; group?: string; action?: React.ReactNode }
export interface NotificationsProps { items: NotificationItem[]; onClear?: () => void; onMarkRead?: () => void; className?: string; style?: Style }
export declare function Notifications(props: NotificationsProps): React.ReactElement;
export interface EmptyStateProps { icon?: IconName; title: React.ReactNode; children?: React.ReactNode; actions?: React.ReactNode; tone?: 'neutral' | 'error'; size?: 'sm' | 'md'; details?: React.ReactNode; className?: string; style?: Style }
export declare function EmptyState(props: EmptyStateProps): React.ReactElement;
export interface ErrorDetailsProps { code?: string; time?: string; logPath?: string; details?: string; defaultOpen?: boolean; onCopy?: () => void; onOpenLog?: () => void; onReport?: () => void; className?: string; style?: Style }
export declare function ErrorDetails(props: ErrorDetailsProps): React.ReactElement;

/* ===================== Inputs ===================== */
export interface ToggleButtonProps extends Omit<React.ButtonHTMLAttributes<HTMLButtonElement>, 'onChange'> {
  pressed?: boolean; defaultPressed?: boolean; onChange?: (pressed: boolean) => void; icon?: IconName; size?: 'sm' | 'md';
}
export declare function ToggleButton(props: ToggleButtonProps): React.ReactElement;
export interface TextAreaProps {
  value?: string; defaultValue?: string; onChange?: (value: string) => void; placeholder?: string;
  rows?: number; autoGrow?: boolean; maxHeight?: number; maxLength?: number; counter?: boolean; mono?: boolean; invalid?: boolean;
  resize?: 'vertical' | 'none'; spellCheck?: boolean; disabled?: boolean; id?: string; 'aria-label'?: string; 'aria-describedby'?: string; className?: string; style?: Style;
}
export declare function TextArea(props: TextAreaProps): React.ReactElement;
export interface SearchFieldProps {
  value?: string; defaultValue?: string; onChange?: (value: string) => void; placeholder?: string;
  shortcut?: string; count?: React.ReactNode; scope?: React.ReactNode; loading?: boolean;
  onClear?: () => void; onSubmit?: (value: string) => void; size?: 'sm' | 'md'; disabled?: boolean; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function SearchField(props: SearchFieldProps): React.ReactElement;
export interface ListItem { value: string; label: string; icon?: IconName; meta?: React.ReactNode; group?: string; disabled?: boolean }
export interface ListBoxProps {
  items: ListItem[]; multiple?: boolean;
  value?: string | string[] | null; defaultValue?: string | string[] | null; onChange?: (value: any) => void;
  /** Double click / Enter. */
  onOpen?: (item: ListItem) => void;
  height?: number; empty?: React.ReactNode; focused?: boolean; label?: string; className?: string; style?: Style;
}
export declare function ListBox(props: ListBoxProps): React.ReactElement;
export interface CheckListProps {
  items: ListItem[]; value?: string[]; defaultValue?: string[]; onChange?: (value: string[]) => void;
  selectAll?: boolean; allLabel?: string; height?: number; label?: string; className?: string; style?: Style;
}
export declare function CheckList(props: CheckListProps): React.ReactElement;
export interface DatePickerProps {
  /** 'YYYY-MM-DD'. Shown as DD.MM.YYYY, weeks start on Monday. */
  value?: string | null; defaultValue?: string | null; onChange?: (value: string | null) => void;
  today?: string; min?: string; max?: string; clearable?: boolean; placeholder?: string; placement?: 'bottom' | 'top';
  invalid?: boolean; size?: 'sm' | 'md'; disabled?: boolean; defaultOpen?: boolean; label?: string; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function DatePicker(props: DatePickerProps): React.ReactElement;
export interface TimePickerProps {
  /** 'HH:MM' or 'HH:MM:SS' (seconds). 24-hour. */
  value?: string; defaultValue?: string; onChange?: (value: string) => void;
  seconds?: boolean; minuteStep?: number; size?: 'sm' | 'md'; disabled?: boolean; defaultOpen?: boolean; label?: string; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function TimePicker(props: TimePickerProps): React.ReactElement;
export interface TimecodeFieldProps {
  /** Frames. */
  value?: number; defaultValue?: number; onChange?: (frame: number) => void;
  fps?: number; dropFrame?: boolean; showFps?: boolean; min?: number; max?: number;
  size?: 'sm' | 'md'; disabled?: boolean; label?: string; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function TimecodeField(props: TimecodeFieldProps): React.ReactElement;
export interface FontInfo { family: string; weights?: number[]; category?: 'sans' | 'serif' | 'mono' | 'display' | string; recent?: boolean; fallback?: string; missing?: boolean }
export interface FontValue { family: string; weight?: number; size?: number }
export interface FontPickerProps {
  fonts: FontInfo[]; value?: FontValue; defaultValue?: FontValue; onChange?: (value: FontValue) => void;
  showSize?: boolean; sample?: string; preview?: boolean; defaultQuery?: string; defaultOpen?: boolean; size?: 'sm' | 'md'; label?: string; className?: string; style?: Style;
}
export declare function FontPicker(props: FontPickerProps): React.ReactElement;
export interface DropZoneProps {
  state?: 'idle' | 'over' | 'uploading' | 'rejected' | 'done';
  title?: React.ReactNode; description?: React.ReactNode; accept?: string; icon?: IconName;
  browseLabel?: string; onBrowse?: () => void; overTitle?: React.ReactNode;
  progress?: number; progressLabel?: React.ReactNode; files?: Array<{ name: string; size?: string }>;
  rejectTitle?: React.ReactNode; rejectText?: React.ReactNode; doneTitle?: React.ReactNode;
  size?: 'sm' | 'md'; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function DropZone(props: DropZoneProps): React.ReactElement;
export interface VectorFieldProps {
  value?: number[]; defaultValue?: number[]; onChange?: (value: number[]) => void;
  dims?: 2 | 3 | 4; labels?: string[]; unit?: string; precision?: number; step?: number;
  linkable?: boolean; linked?: boolean; defaultLinked?: boolean; onLinkedChange?: (linked: boolean) => void;
  mixed?: boolean[]; size?: 'sm' | 'md'; disabled?: boolean; label?: string; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function VectorField(props: VectorFieldProps): React.ReactElement;
export interface TransformValue { position: number[]; rotation: number[]; scale: number[] }
export interface TransformControlProps {
  value?: TransformValue; defaultValue?: TransformValue; onChange?: (value: TransformValue) => void;
  space?: 'global' | 'local' | 'parent'; defaultSpace?: 'global' | 'local' | 'parent'; onSpaceChange?: (space: string) => void; showSpace?: boolean;
  mixed?: Partial<Record<'position' | 'rotation' | 'scale', boolean[]>>; linkedScale?: boolean; unit?: string;
  title?: React.ReactNode; label?: string; size?: 'sm' | 'md'; className?: string; style?: Style;
}
export declare function TransformControl(props: TransformControlProps): React.ReactElement;

/* ===================== Selectors & collections ===================== */
export interface PresetItem { value: string; label: string; group?: string; description?: React.ReactNode; builtin?: boolean }
export interface PresetSelectorProps {
  items: PresetItem[]; value?: string; defaultValue?: string; onChange?: (value: string) => void;
  /** Current values differ from the preset: «змінено» mark; built-ins can't be overwritten. */
  modified?: boolean;
  onSave?: () => void; onSaveAs?: () => void; onReset?: () => void; onManage?: () => void; manageLabel?: string; saveShortcut?: string;
  /** 'layout' for window layouts. */
  icon?: IconName; label?: string; showLabel?: boolean; placeholder?: string;
  ghost?: boolean; size?: 'sm' | 'md'; popWidth?: number; align?: 'start' | 'end'; placement?: 'down' | 'up'; defaultOpen?: boolean; disabled?: boolean; className?: string; style?: Style;
}
export declare function PresetSelector(props: PresetSelectorProps): React.ReactElement;
export interface Profile { value: string; name: string; meta?: React.ReactNode; initials?: string }
export interface ProfileSelectorProps {
  profiles: Profile[]; value?: string; defaultValue?: string; onChange?: (value: string) => void;
  onManage?: () => void; onImport?: () => void; compact?: boolean; ghost?: boolean; size?: 'sm' | 'md'; align?: 'start' | 'end'; defaultOpen?: boolean; className?: string; style?: Style;
}
export declare function ProfileSelector(props: ProfileSelectorProps): React.ReactElement;
export interface Device { value: string; name: string; kind?: string; meta?: React.ReactNode; status?: 'ok' | 'busy' | 'offline'; statusText?: string; isDefault?: boolean }
export interface DeviceSelectorProps {
  devices: Device[]; value?: string; defaultValue?: string; onChange?: (value: string) => void;
  kind?: 'gpu' | 'cpu' | 'audio' | 'mic' | 'display' | string; onRefresh?: () => void; placeholder?: string;
  size?: 'sm' | 'md'; disabled?: boolean; popWidth?: number; defaultOpen?: boolean; label?: string; className?: string; style?: Style;
}
export declare function DeviceSelector(props: DeviceSelectorProps): React.ReactElement;
export interface SortControlProps {
  options: Array<{ value: string; label: string }>; value?: string; defaultValue?: string; onChange?: (value: string) => void;
  direction?: 'asc' | 'desc'; defaultDirection?: 'asc' | 'desc'; onDirectionChange?: (d: 'asc' | 'desc') => void;
  prefix?: string; align?: 'start' | 'end'; defaultOpen?: boolean; className?: string; style?: Style;
}
export declare function SortControl(props: SortControlProps): React.ReactElement;
export interface ViewSwitcherProps {
  value?: string; defaultValue?: string; onChange?: (value: string) => void;
  views?: Array<{ value: string; icon: IconName; label: string; shortcut?: string }>;
  /** Tile size 0–1, shown only for 'grid'. */
  size?: number; defaultSize?: number; onSizeChange?: (size: number) => void; showSize?: boolean; className?: string; style?: Style;
}
export declare function ViewSwitcher(props: ViewSwitcherProps): React.ReactElement;
export interface FilterDef { key: string; label: string; options: Array<{ value: string; label: string; count?: number; icon?: IconName }> }
export interface FilterBarProps {
  query?: string; defaultQuery?: string; onQueryChange?: (q: string) => void; placeholder?: string; shortcut?: string;
  filters?: FilterDef[]; values?: Record<string, string[]>; defaultValues?: Record<string, string[]>; onValuesChange?: (values: Record<string, string[]>) => void;
  count?: React.ReactNode; sort?: React.ReactNode; view?: React.ReactNode; defaultOpenFilter?: string; className?: string; style?: Style;
}
export declare function FilterBar(props: FilterBarProps): React.ReactElement;

/* ===================== Navigation ===================== */
export interface MenuBarMenu { label: string; items?: MenuItem[]; defaultSubmenu?: number }
/** Menus that don't fit the available width go under «…» as submenus. */
export interface MenuBarProps { menus: Array<string | MenuBarMenu>; defaultActive?: string; onSelect?: (item: MenuItem, menu: MenuBarMenu) => void; label?: string; className?: string }
export declare function MenuBar(props: MenuBarProps): React.ReactElement;
export interface ContextMenuProps {
  /** Window coordinates of the pointer; the menu flips left / up at the edges. */
  x: number; y: number; items: MenuItem[];
  onSelect?: (item: MenuItem) => void; onClose?: () => void; defaultSubmenu?: number; inline?: boolean; label?: string; className?: string;
}
export declare function ContextMenu(props: ContextMenuProps): React.ReactElement;
export interface AccordionItem { id: string; title: React.ReactNode; icon?: IconName; summary?: React.ReactNode; aside?: React.ReactNode; content: React.ReactNode; disabled?: boolean }
export interface AccordionProps { items: AccordionItem[]; multiple?: boolean; value?: string[]; defaultValue?: string[]; onChange?: (open: string[]) => void; className?: string; style?: Style }
export declare function Accordion(props: AccordionProps): React.ReactElement;
export interface SidebarItem { id: string; label: string; icon?: IconName; count?: React.ReactNode; status?: Status; disabled?: boolean; children?: SidebarItem[] }
export interface SidebarProps {
  sections: Array<{ title?: string; items: SidebarItem[] }>; value?: string; defaultValue?: string; onChange?: (id: string) => void;
  collapsible?: boolean; collapsed?: boolean; defaultCollapsed?: boolean; onCollapsedChange?: (c: boolean) => void;
  footer?: React.ReactNode; width?: number; label?: string; className?: string; style?: Style;
}
export declare function Sidebar(props: SidebarProps): React.ReactElement;
export interface Crumb { label: string; icon?: IconName }
export interface BreadcrumbsProps { items: Crumb[]; maxItems?: number; onNavigate?: (item: Crumb) => void; label?: string; className?: string; style?: Style }
export declare function Breadcrumbs(props: BreadcrumbsProps): React.ReactElement;
export interface PaginationProps {
  page?: number; defaultPage?: number; onChange?: (page: number) => void; pageCount?: number; total?: number;
  pageSize?: number; pageSizes?: number[]; onPageSizeChange?: (size: number) => void; compact?: boolean; label?: string; className?: string; style?: Style;
}
export declare function Pagination(props: PaginationProps): React.ReactElement;
export interface ScrollAreaProps {
  children?: React.ReactNode; height?: number | string; maxHeight?: number | string;
  /** Edge shadows when there is more content. Default true. */
  shadows?: boolean;
  /** Reserve the scrollbar gutter so content doesn't jump. */
  always?: boolean;
  initialScroll?: number; orientation?: 'vertical' | 'horizontal' | 'both'; focusable?: boolean; role?: string; label?: string; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function ScrollArea(props: ScrollAreaProps): React.ReactElement;

/* ===================== Media & canvas ===================== */
export interface ScrubberProps {
  fps?: number; start: number; end: number; value?: number; defaultValue?: number; onChange?: (frame: number) => void;
  inPoint?: number; outPoint?: number; cached?: Array<[number, number]>; markers?: Array<{ frame: number; label?: string }>;
  hoverFrame?: number; showTime?: boolean; label?: string; className?: string; style?: Style;
}
export declare function Scrubber(props: ScrubberProps): React.ReactElement;
export interface ZoomControlProps {
  /** 1 = 100 %. */
  value?: number; defaultValue?: number; onChange?: (zoom: number) => void; min?: number; max?: number; steps?: number[];
  onFit?: () => void; onFitSelection?: () => void;
  hand?: boolean; defaultHand?: boolean; onHandChange?: (on: boolean) => void;
  floating?: boolean; placement?: 'down' | 'up'; defaultOpen?: boolean; label?: string; className?: string; style?: Style;
}
export declare function ZoomControl(props: ZoomControlProps): React.ReactElement;
export interface CanvasView { x: number; y: number; zoom: number }
export interface CanvasLink { from: [string, string]; to: [string, string]; selected?: boolean; dashed?: boolean }
export interface CanvasProps {
  children?: React.ReactNode; view?: CanvasView; defaultView?: CanvasView; onViewChange?: (v: CanvasView) => void;
  grid?: 'dots' | 'lines' | false; gridSize?: number; links?: CanvasLink[];
  marquee?: { x: number; y: number; w: number; h: number }; controls?: React.ReactNode; hud?: React.ReactNode;
  wheel?: 'zoom' | 'pan'; minZoom?: number; maxZoom?: number; defaultHand?: boolean; onFit?: () => void; height?: number | string; label?: string; className?: string; style?: Style;
}
export interface CanvasNodeProps {
  id?: string; x: number; y: number; width?: number; title: React.ReactNode; icon?: IconName;
  inputs?: Array<{ id: string; label: string }>; outputs?: Array<{ id: string; label: string }>;
  selected?: boolean; error?: boolean; children?: React.ReactNode; className?: string; style?: Style;
}
export declare function Canvas(props: CanvasProps): React.ReactElement;
export declare namespace Canvas { function Node(props: CanvasNodeProps): React.ReactElement; }
export interface Keyframe { frame: number; value: number; interp?: 'constant' | 'linear' | 'bezier'; angle?: number }
export interface KeyChannel { id: string; name: string; color?: string; keys: Keyframe[] }
export interface KeyframeEditorProps {
  channels: KeyChannel[]; start: number; end: number; fps?: number; frame?: number; valueRange?: [number, number];
  defaultSelected?: { channel: string; index: number } | string; defaultChannels?: string[]; height?: number;
  onChange?: (channels: KeyChannel[]) => void; onFit?: () => void; label?: string; className?: string; style?: Style;
}
export declare function KeyframeEditor(props: KeyframeEditorProps): React.ReactElement;

/* ===================== Data & monitoring ===================== */
export interface PlotSeries { label: string; color?: string; data: Array<[number, number]> }
export interface PlotProps {
  /** Up to 8 series; colours chart-1…8 in fixed order. One y-axis. */
  series: PlotSeries[]; title?: React.ReactNode; unit?: string; height?: number;
  xFormat?: (v: number) => string; yFormat?: (v: number) => string; xLabel?: string; xDomain?: [number, number]; yDomain?: [number, number];
  thresholds?: Array<{ y: number; label?: string; tone?: 'warning' | 'danger' | 'neutral' }>;
  hoverIndex?: number; area?: boolean; directLabels?: boolean; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function Plot(props: PlotProps): React.ReactElement;
export interface HistogramProps {
  mode?: 'rgb' | 'luma'; data?: { r?: number[]; g?: number[]; b?: number[]; l?: number[] }; bins?: number;
  /** Clipped share, %. */
  clip?: { shadows?: number; highlights?: number }; clipLimit?: number; height?: number; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function Histogram(props: HistogramProps): React.ReactElement;
export interface WaveformProps { peaks?: number[]; seed?: number; height?: number; barWidth?: number; progress?: number; selection?: [number, number]; 'aria-label'?: string; className?: string; style?: Style }
export declare function Waveform(props: WaveformProps): React.ReactElement;
export interface MeterProps {
  variant?: 'bar' | 'level' | 'radial';
  /** bar / radial: 0–1. */
  value?: number; label?: React.ReactNode; detail?: React.ReactNode;
  /** Thresholds 0–1 (default 0.75 / 0.9). Above 1 — never warn (utilisation where high is good). */
  warning?: number; danger?: number; showThresholds?: boolean; format?: (v: number) => string; size?: 'sm' | 'md';
  /** level: dB per channel, peak hold, range −60…+6. */
  channels?: number[]; peaks?: number[]; min?: number; max?: number; warnDb?: number; dangerDb?: number;
  orientation?: 'horizontal' | 'vertical'; scale?: number[]; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function Meter(props: MeterProps): React.ReactElement;
export interface LogLine { id?: string | number; time?: string; level?: 'error' | 'warn' | 'info' | 'debug'; source?: string; text: string; count?: number }
export interface LogViewerProps {
  lines: LogLine[]; levels?: Partial<Record<'error' | 'warn' | 'info' | 'debug', boolean>>; defaultLevels?: Partial<Record<'error' | 'warn' | 'info' | 'debug', boolean>>;
  onLevelsChange?: (levels: Record<string, boolean>) => void; query?: string; defaultQuery?: string; onQueryChange?: (q: string) => void;
  defaultWrap?: boolean; defaultFollow?: boolean; onCopy?: () => void; onClear?: () => void; height?: number | string; selected?: string | number;
  toolbar?: boolean; label?: string; className?: string; style?: Style;
}
export declare function LogViewer(props: LogViewerProps): React.ReactElement;
export interface ConsoleLine { kind?: 'input' | 'output' | 'result' | 'error' | 'warn'; text: string; id?: string | number }
export interface ConsoleProps {
  lines?: ConsoleLine[]; suggestions?: string[]; prompt?: string; onRun?: (command: string) => void; history?: string[];
  height?: number | string; placeholder?: string; inputLabel?: string; defaultValue?: string; label?: string; className?: string; style?: Style;
}
export declare function Console(props: ConsoleProps): React.ReactElement;
export interface TaskItem { title: React.ReactNode; detail?: React.ReactNode; status?: 'done' | 'running' | 'pending' | 'warning' | 'error' | 'skipped'; progress?: number; meta?: React.ReactNode; action?: React.ReactNode }
export interface TaskListProps { title?: React.ReactNode; summary?: React.ReactNode; items: TaskItem[]; className?: string; style?: Style }
export declare function TaskList(props: TaskListProps): React.ReactElement;
export interface QueueJob { id: string | number; name: string; status?: 'running' | 'queued' | 'paused' | 'failed' | 'done'; progress?: number; preset?: React.ReactNode; detail?: React.ReactNode }
export interface QueueProps {
  jobs?: QueueJob[]; defaultJobs?: QueueJob[]; onReorder?: (jobs: QueueJob[]) => void;
  /** false: no title (the queue sits in a panel that already has one). */
  title?: React.ReactNode | false; summary?: React.ReactNode; actions?: React.ReactNode; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function Queue(props: QueueProps): React.ReactElement;

/* ===================== Application settings ===================== */
/** Where a setting's value comes from. `policy` — set by the studio, visible but locked. */
export type SettingSource = 'default' | 'profile' | 'project' | 'policy';
export interface SettingsNavItem {
  id: string; label: string; icon?: IconName; group?: string; description?: string;
  count?: React.ReactNode; status?: Status;
  /** A section with its own content (e.g. KeymapEditor) instead of Settings.Section rows. */
  external?: boolean; content?: React.ReactNode;
  /** No profile / project switch for this section. */
  noScope?: boolean;
}
export interface SettingsProps {
  sections: SettingsNavItem[];
  value?: string; defaultValue?: string; onChange?: (sectionId: string) => void;
  query?: string; defaultQuery?: string; onQueryChange?: (q: string) => void;
  scopes?: Array<{ value: 'profile' | 'project' | string; label: string; icon?: IconName }>;
  scope?: string; defaultScope?: string; onScopeChange?: (scope: string) => void;
  /** Show only settings that differ from the defaults. */
  changedOnly?: boolean; defaultChangedOnly?: boolean; onChangedOnlyChange?: (v: boolean) => void;
  /** Shown under the header while the project scope is selected. */
  projectNote?: React.ReactNode;
  /** A strip under the header, e.g. InfoBar layout="banner" about a pending restart. */
  banner?: React.ReactNode;
  footer?: React.ReactNode; navFooter?: React.ReactNode;
  /** Empty search result: offer to search the key map. */
  onSearchKeymap?: () => void;
  children?: React.ReactNode; className?: string; style?: Style;
}
export interface SettingsSectionProps {
  /** Id of the nav section this group belongs to. */
  section: string; title?: React.ReactNode; aside?: React.ReactNode; description?: React.ReactNode;
  /** Section actions: export, «Скинути все…». */
  footer?: React.ReactNode; children?: React.ReactNode;
  /** Start with rows unavailable on this machine shown (disabled, with the reason). */
  defaultShowUnavailable?: boolean;
}
export interface SettingsRowProps {
  label: string; description?: string;
  /** Key in settings.json, e.g. 'files.autosave'; searchable, shown on hover. */
  id?: string; keywords?: string; anchor?: string;
  source?: SettingSource;
  /** Stored on this computer only — never synced with the profile. */
  machine?: boolean;
  /** Can't be set at the project level. */
  profileOnly?: boolean;
  /** Takes effect after a restart; `pending` — changed and waiting for it. */
  restart?: boolean; pending?: boolean;
  /** Why the value is what it is (policy, another setting). */
  overriddenBy?: React.ReactNode; warning?: React.ReactNode;
  resetTo?: string; onReset?: (() => void) | null;
  /** Waits for another setting: `dependsMet={false}` keeps the row in place, disabled, with «Діє, коли увімкнено …». */
  dependsOn?: string; dependsMet?: boolean;
  /** Subordinate to the row above (drawn with a connector). */
  indent?: boolean;
  /** What the setting needs, checked against `caps`; or a ready `availability`. Platform-unavailable rows are hidden (the section says how many and why); fixable ones show the reason and `onFix`. */
  requires?: Requires; caps?: Capabilities; availability?: Availability; onFix?: () => void;
  notice?: { tone?: 'info' | 'warning'; text: React.ReactNode; action?: React.ReactNode };
  /** 'stack': the control spans the full width under the label. */
  layout?: 'row' | 'stack';
  children?: React.ReactNode; className?: string;
}
/** The settings window body: search and sections on the left, rows on the right. Changes apply immediately — no OK / Apply. */
export declare function Settings(props: SettingsProps): React.ReactElement;
export declare namespace Settings {
  function Section(props: SettingsSectionProps): React.ReactElement | null;
  function Row(props: SettingsRowProps): React.ReactElement | null;
}

export interface KbdProps {
  /** 'Ctrl+Shift+P', or the parts of a sequence: ['Ctrl+K', 'Ctrl+S']. */
  keys: string | string[];
  platform?: 'windows' | 'macos' | 'linux'; size?: 'sm' | 'md'; tone?: 'warning';
  'aria-label'?: string; className?: string; style?: Style;
}
export declare function Kbd(props: KbdProps): React.ReactElement;
export interface ShortcutInputProps {
  /** Normalised: modifiers in the order Ctrl · Cmd · Win · Shift · Alt, then the key (physical key, Latin letter). */
  value?: string; defaultValue?: string; onChange?: (combo: string) => void;
  recording?: boolean; defaultRecording?: boolean; onRecordingChange?: (r: boolean) => void;
  /** Mockups: modifiers shown as held while recording. */
  previewModifiers?: string[];
  conflict?: React.ReactNode; onReplace?: () => void; onCancelConflict?: () => void;
  platform?: 'windows' | 'macos' | 'linux'; placeholder?: string; recordingText?: string; clearable?: boolean;
  autoFocus?: boolean; keepRecording?: boolean;
  size?: 'sm' | 'md'; disabled?: boolean; label?: string; 'aria-label'?: string; className?: string; style?: Style;
}
export declare function ShortcutInput(props: ShortcutInputProps): React.ReactElement;
export interface KeymapAction {
  id: string; label: string;
  /** Menu path: ['Редагування', 'Об’єкт'] — the first part groups the list. */
  path?: string[];
  context: 'global' | 'viewport' | 'timeline' | 'nodes' | 'text' | string;
  keys: string[]; defaultKeys?: string[]; keywords?: string;
}
export interface KeymapEditorProps {
  actions?: KeymapAction[]; defaultActions?: KeymapAction[]; onChange?: (actions: KeymapAction[]) => void;
  contexts?: Array<{ value: string; label: string }>;
  schemes?: PresetItem[]; scheme?: string; defaultScheme?: string; onSchemeChange?: (v: string) => void;
  onSaveScheme?: () => void; onResetScheme?: () => void; onManageSchemes?: () => void;
  platform?: 'windows' | 'macos' | 'linux'; height?: number | string; footer?: React.ReactNode; label?: string;
  /** Mockups. */
  defaultQuery?: string; defaultKeyQuery?: string; defaultContext?: string; defaultChangedOnly?: boolean;
  defaultEditing?: { id: string; index: number }; defaultPending?: { id: string; index: number; combo: string; other?: { label: string } };
  className?: string; style?: Style;
}
/** Key map editor: search by action or by pressing keys, contexts, conflicts shown before saving, several keys per action. */
export declare function KeymapEditor(props: KeymapEditorProps): React.ReactElement;
export interface KeyboardMapProps {
  /** 'Ctrl+S' → 'Зберегти' or { label, conflict }. */
  bindings: Record<string, string | { label: string; conflict?: string }>;
  modifiers?: string[]; defaultModifiers?: string[]; onModifiersChange?: (m: string[]) => void;
  selected?: string | null; defaultSelected?: string | null; onSelect?: (combo: string) => void;
  onKeyClick?: (combo: string, info?: { label: string; conflict?: string }) => void;
  /** Key size, px. Default 40. */
  unit?: number; label?: string; className?: string; style?: Style;
}
export declare function KeyboardMap(props: KeyboardMapProps): React.ReactElement;

/* ===================== Availability & dependencies ===================== */
export type Vendor = 'nvidia' | 'amd' | 'intel' | 'apple';
/** What an option or a setting needs. */
export interface Requires {
  os?: Array<'windows' | 'macos' | 'linux'>;
  gpu?: Vendor[];
  /** GPU capability, not a model name: 'h264-enc', 'hevc-enc', 'av1-enc', 'optix', 'hip'… */
  feature?: string | string[]; featureLabel?: string;
  /** Minimal driver per vendor: { nvidia: 530 }. */
  driver?: Partial<Record<Vendor, number>>;
  licence?: 'pro' | 'studio' | string;
  plugin?: string; pluginLabel?: string;
  setting?: { id: string; label: string };
  /** Doesn't fit the current choice — the reason: 'ProRes — лише в MOV'. */
  incompatible?: string;
}
/** What this machine (or the target, e.g. a render farm) has. */
export interface Capabilities {
  os?: 'windows' | 'macos' | 'linux';
  gpus?: Array<{ vendor: Vendor; name: string; features?: string[]; driver?: number }>;
  licence?: string; plugins?: string[]; settings?: Record<string, boolean>;
}
export interface Availability {
  available: boolean;
  /** platform — can't exist here: hide · fixable — driver / extension / licence: show disabled with `fix` · dependent — waits for another setting · incompatible — doesn't fit the current choice. */
  kind?: 'platform' | 'fixable' | 'dependent' | 'incompatible';
  reason?: string; fix?: string;
  /** The GPU that provides it. */
  device?: { vendor: Vendor; name: string };
}
export declare function availability(requires: Requires | null | undefined, caps: Capabilities): Availability;

export interface OptionItem {
  value: string; label: string; group?: string; description?: React.ReactNode; icon?: IconName; tag?: string;
  requires?: Requires; availability?: Availability; available?: boolean; kind?: Availability['kind']; reason?: string;
}
export interface OptionPickerProps {
  items: OptionItem[]; caps?: Capabilities;
  value?: string; defaultValue?: string; onChange?: (value: string) => void;
  /** Also list what can't work here (disabled, with the reason). Default: hidden and counted at the bottom. */
  showUnavailable?: boolean; defaultShowUnavailable?: boolean; onShowUnavailableChange?: (v: boolean) => void;
  onFix?: (item: OptionItem, availability: Availability) => void;
  /** The current value is unavailable here (a preset from another machine): what will be used instead. */
  fallbackLabel?: string; onReplace?: () => void; replaceLabel?: string;
  notice?: { tone?: 'info' | 'warning'; text: React.ReactNode; action?: React.ReactNode };
  unavailableLabel?: string; incompatibleLabel?: string;
  icon?: IconName; placeholder?: string; label?: string; 'aria-label'?: string;
  size?: 'sm' | 'md'; disabled?: boolean; ghost?: boolean; placement?: 'down' | 'up'; align?: 'start' | 'end'; popWidth?: number; defaultOpen?: boolean;
  className?: string; style?: Style;
}
/** A select whose options depend on hardware, OS, licence or another choice: shows what works here, counts and explains the rest. */
export declare function OptionPicker(props: OptionPickerProps): React.ReactElement;

/** Aliases. */
export declare const Switch: typeof Toggle;
export declare const SpinBox: typeof NumberField;

/** Frames to HH:MM:SS:FF (short: MM:SS:FF). */
export declare function timecode(frame: number, fps: number, short?: boolean): string;

declare global {
  interface Window {
    Anvil: {
      Icon: typeof Icon; Button: typeof Button; IconButton: typeof IconButton; SegmentedControl: typeof SegmentedControl; Toolbar: typeof Toolbar;
      TextField: typeof TextField; NumberField: typeof NumberField; Vector3Field: typeof Vector3Field; Slider: typeof Slider;
      Checkbox: typeof Checkbox; Toggle: typeof Toggle; Select: typeof Select;
      TitleBar: typeof TitleBar; Workspace: typeof Workspace; PanelStack: typeof PanelStack; Panel: typeof Panel; Tabs: typeof Tabs; PropertyGrid: typeof PropertyGrid; TreeView: typeof TreeView; StatusBar: typeof StatusBar;
      Viewport: typeof Viewport; Timeline: typeof Timeline; TransportBar: typeof TransportBar; ProgressBar: typeof ProgressBar; RenderJob: typeof RenderJob;
      Badge: typeof Badge; InfoBar: typeof InfoBar; Menu: typeof Menu; Tooltip: typeof Tooltip; ToolWindow: typeof ToolWindow; DockGuide: typeof DockGuide;
      ColorPicker: typeof ColorPicker; GradientEditor: typeof GradientEditor; RangeSlider: typeof RangeSlider; Knob: typeof Knob;
      RadioGroup: typeof RadioGroup; Combobox: typeof Combobox; TagInput: typeof TagInput; DataTable: typeof DataTable;
      PathField: typeof PathField; AssetSlot: typeof AssetSlot; CurveEditor: typeof CurveEditor; Dialog: typeof Dialog; CommandPalette: typeof CommandPalette;
      SplitButton: typeof SplitButton; ToolFlyout: typeof ToolFlyout; timecode: typeof timecode;
      Splitter: typeof Splitter; DockLayout: typeof DockLayout; Label: typeof Label; Heading: typeof Heading; HelperText: typeof HelperText; Field: typeof Field;
      Popover: typeof Popover; TeachingTip: typeof TeachingTip; Spinner: typeof Spinner; StatusIndicator: typeof StatusIndicator; Toast: typeof Toast; ToastStack: typeof ToastStack;
      Notifications: typeof Notifications; EmptyState: typeof EmptyState; ErrorDetails: typeof ErrorDetails;
      ToggleButton: typeof ToggleButton; Switch: typeof Switch; SpinBox: typeof SpinBox; TextArea: typeof TextArea; SearchField: typeof SearchField; ListBox: typeof ListBox; CheckList: typeof CheckList;
      DatePicker: typeof DatePicker; TimePicker: typeof TimePicker; TimecodeField: typeof TimecodeField; FontPicker: typeof FontPicker; DropZone: typeof DropZone; VectorField: typeof VectorField; TransformControl: typeof TransformControl;
      PresetSelector: typeof PresetSelector; ProfileSelector: typeof ProfileSelector; DeviceSelector: typeof DeviceSelector; SortControl: typeof SortControl; ViewSwitcher: typeof ViewSwitcher; FilterBar: typeof FilterBar;
      MenuBar: typeof MenuBar; ContextMenu: typeof ContextMenu; Accordion: typeof Accordion; Sidebar: typeof Sidebar; Breadcrumbs: typeof Breadcrumbs; Pagination: typeof Pagination; ScrollArea: typeof ScrollArea;
      Scrubber: typeof Scrubber; ZoomControl: typeof ZoomControl; Canvas: typeof Canvas; KeyframeEditor: typeof KeyframeEditor;
      Plot: typeof Plot; Histogram: typeof Histogram; Waveform: typeof Waveform; Meter: typeof Meter; LogViewer: typeof LogViewer; Console: typeof Console; TaskList: typeof TaskList; Queue: typeof Queue;
      Settings: typeof Settings; Kbd: typeof Kbd; ShortcutInput: typeof ShortcutInput; KeymapEditor: typeof KeymapEditor; KeyboardMap: typeof KeyboardMap;
      OptionPicker: typeof OptionPicker; availability: typeof availability;
    };
  }
}
