import React, { useState, useEffect, useRef } from 'react';
import { tokenizeIntoSlots, SlotToken } from '@shared/inflections';
import { HelpCircle, RefreshCw } from 'lucide-react';

interface LetterSlotInputProps {
  targetText: string;
  onComplete: () => void;
  onError?: () => void;
  onHintUsed?: () => void;
  showHintButton?: boolean;
  disabled?: boolean;
  autoFocus?: boolean;
  hintWarning?: string | null;
}

// 简易 Web Audio 音效合成器
function playSfx(type: 'type' | 'success' | 'error') {
  if (typeof window === 'undefined') return;
  try {
    const ctx = new (window.AudioContext || (window as any).webkitAudioContext)();
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.connect(gain);
    gain.connect(ctx.destination);

    if (type === 'type') {
      // 类似打字机清脆音
      osc.frequency.setValueAtTime(600, ctx.currentTime);
      gain.gain.setValueAtTime(0.08, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.05);
      osc.start(ctx.currentTime);
      osc.stop(ctx.currentTime + 0.05);
    } else if (type === 'success') {
      // 欢快双音节
      osc.frequency.setValueAtTime(523.25, ctx.currentTime); // C5
      osc.frequency.setValueAtTime(659.25, ctx.currentTime + 0.08); // E5
      gain.gain.setValueAtTime(0.12, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.2);
      osc.start(ctx.currentTime);
      osc.stop(ctx.currentTime + 0.2);
    } else if (type === 'error') {
      // 低沉嗡鸣
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(150, ctx.currentTime);
      gain.gain.setValueAtTime(0.15, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.15);
      osc.start(ctx.currentTime);
      osc.stop(ctx.currentTime + 0.15);
    }
  } catch {
    // 忽略音频上下文静音限制
  }
}

export const LetterSlotInput: React.FC<LetterSlotInputProps> = ({
  targetText,
  onComplete,
  onError,
  onHintUsed,
  showHintButton = true,
  disabled = false,
  autoFocus = true,
  hintWarning = null,
}) => {
  const [tokens, setTokens] = useState<SlotToken[]>([]);
  const [enteredLetters, setEnteredLetters] = useState<string[]>([]);
  const [isShaking, setIsShaking] = useState(false);
  const [showPeekHint, setShowPeekHint] = useState(false);
  const inputRef = useRef<HTMLInputElement>(null);

  // 初始化槽位结构
  useEffect(() => {
    const t = tokenizeIntoSlots(targetText);
    setTokens(t);
    setEnteredLetters(new Array(t.length).fill(''));
    setShowPeekHint(false);

    if (autoFocus && !disabled) {
      setTimeout(() => inputRef.current?.focus(), 50);
    }
  }, [targetText, autoFocus, disabled]);

  // 计算当前需要填入的有效槽位索引
  const getCurrentSlotIndex = (currentLetters: string[]): number => {
    for (let i = 0; i < tokens.length; i++) {
      if (tokens[i].isSlot && !currentLetters[i]) {
        return i;
      }
    }
    return tokens.length; // 已全部填满
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (disabled || isShaking) return;

    if (e.key === 'Backspace') {
      e.preventDefault();
      // 回退上一个已填入的字母槽位
      const updated = [...enteredLetters];
      for (let i = updated.length - 1; i >= 0; i--) {
        if (tokens[i]?.isSlot && updated[i]) {
          updated[i] = '';
          setEnteredLetters(updated);
          playSfx('type');
          break;
        }
      }
      return;
    }

    if (e.key.length === 1) {
      const char = e.key;
      const currentIdx = getCurrentSlotIndex(enteredLetters);
      if (currentIdx >= tokens.length) return; // 已满

      e.preventDefault();
      const expectedChar = tokens[currentIdx].char;

      // 实时字母大小写无关校验
      if (char.toLowerCase() === expectedChar.toLowerCase()) {
        const updated = [...enteredLetters];
        updated[currentIdx] = expectedChar; // 保持目标大小写
        setEnteredLetters(updated);
        playSfx('type');

        // 检查是否全部槽位填满
        const nextIdx = getCurrentSlotIndex(updated);
        if (nextIdx >= tokens.length) {
          playSfx('success');
          setTimeout(onComplete, 250);
        }
      } else {
        // 敲错字母：震动、变红、低音提醒
        playSfx('error');
        setIsShaking(true);
        if (onError) onError();
        setTimeout(() => setIsShaking(false), 300);
      }
    }
  };

  const handlePeekHint = () => {
    setShowPeekHint(true);
    if (onHintUsed) onHintUsed();
    // 3秒后隐藏提示
    setTimeout(() => setShowPeekHint(false), 3000);
  };

  return (
    <div className="flex flex-col items-center justify-center space-y-4 my-4 w-full">
      {/* 隐藏但处于焦点的键盘捕获 input */}
      <input
        ref={inputRef}
        type="text"
        className="opacity-0 absolute -z-10 h-0 w-0"
        onKeyDown={handleKeyDown}
        autoFocus={autoFocus}
        disabled={disabled}
      />

      {/* 同音词/提示警告气泡 */}
      {hintWarning && (
        <div className="px-4 py-2 rounded-xl bg-amber-50 border border-amber-200 text-amber-800 text-xs font-medium animate-bounce">
          {hintWarning}
        </div>
      )}

      {/* 短暂看一眼提示 */}
      {showPeekHint && (
        <div className="px-4 py-1.5 rounded-full bg-blue-100 text-blue-800 text-xs font-mono font-bold tracking-wider animate-in fade-in">
          正确拼写：{targetText}
        </div>
      )}

      {/* 字母格子容器 (点击任意处自动聚焦隐藏 input) */}
      <div
        onClick={() => inputRef.current?.focus()}
        className={`flex flex-wrap items-center justify-center gap-1.5 sm:gap-2.5 p-3 rounded-2xl cursor-text select-none transition-transform duration-150 ${
          isShaking ? 'translate-x-[-8px] animate-shake' : ''
        }`}
      >
        {tokens.map((token, idx) => {
          if (!token.isSlot) {
            // 空格或连字符等分隔符
            return (
              <span
                key={idx}
                className="text-slate-400 font-bold text-lg sm:text-2xl px-1"
              >
                {token.char}
              </span>
            );
          }

          const filled = enteredLetters[idx];
          const isCurrentCursor = idx === getCurrentSlotIndex(enteredLetters);

          return (
            <div
              key={idx}
              className={`w-9 h-12 sm:w-12 sm:h-16 rounded-xl flex items-center justify-center font-mono text-xl sm:text-2xl font-bold transition-all border-b-4 ${
                filled
                  ? 'bg-emerald-50 text-emerald-800 border-emerald-500 shadow-sm'
                  : isCurrentCursor
                  ? 'bg-blue-50/70 border-blue-500 text-slate-800 ring-2 ring-blue-500/20'
                  : 'bg-slate-100 border-slate-300 text-transparent'
              }`}
            >
              {filled || (isCurrentCursor ? <span className="animate-pulse text-blue-500">_</span> : '')}
            </div>
          );
        })}
      </div>

      {/* 辅助按钮栏 */}
      {showHintButton && (
        <div className="flex items-center gap-3">
          <button
            type="button"
            onClick={handlePeekHint}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs font-medium text-slate-500 hover:text-blue-600 hover:bg-blue-50 transition-colors"
          >
            <HelpCircle className="w-3.5 h-3.5" />
            看一眼提示 (重置本词)
          </button>
        </div>
      )}
    </div>
  );
};
