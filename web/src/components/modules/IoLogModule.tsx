import React, { useEffect, useRef } from 'react';
import type { IoLogEntry } from '../../types/telemetry';
import { Terminal } from 'lucide-react';

interface IoLogModuleProps {
  logs: IoLogEntry[];
}

export const IoLogModule: React.FC<IoLogModuleProps> = ({ logs }) => {
  const scrollRef = useRef<HTMLDivElement>(null);
  const shouldStickToBottomRef = useRef(true);

  const handleScroll = () => {
    const el = scrollRef.current;
    if (!el) return;

    // Keep sticky mode only while user stays near the bottom.
    const distanceFromBottom = el.scrollHeight - el.scrollTop - el.clientHeight;
    shouldStickToBottomRef.current = distanceFromBottom < 24;
  };

  useEffect(() => {
    // Auto-scroll only if user has not scrolled up to inspect older logs.
    if (scrollRef.current && shouldStickToBottomRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [logs]);

  const getLogColor = (type: IoLogEntry['type']) => {
    switch (type) {
      case 'ERROR': return 'text-red-400';
      case 'WARN': return 'text-yellow-400';
      case 'DATA': return 'text-emerald-400';
      case 'INFO': default: return 'text-slate-300';
    }
  };

  return (
    <div className="bg-slate-900 border border-slate-700 rounded-xl overflow-hidden flex flex-col h-full shadow-lg">
      <div className="bg-slate-800 border-b border-slate-700/50 p-3 flex items-center gap-2">
        <Terminal className="h-4 w-4 text-slate-400" />
        <span className="font-mono text-xs text-slate-300 tracking-wide uppercase">Real-time I/O Log</span>
        <div className="ml-auto flex items-center gap-2">
           <div className="h-2 w-2 rounded-full bg-emerald-500 animate-pulse"></div>
           <span className="text-[10px] text-slate-500 uppercase tracking-widest">Live</span>
        </div>
      </div>
      
      <div ref={scrollRef} onScroll={handleScroll} className="flex-1 p-4 font-mono text-xs overflow-y-auto space-y-1 custom-scrollbar" style={{ maxHeight: '300px' }}>
        {[...logs].reverse().map(log => {
          const date = new Date(log.timestamp);
          const timeString = `${date.getHours().toString().padStart(2, '0')}:${date.getMinutes().toString().padStart(2, '0')}:${date.getSeconds().toString().padStart(2, '0')}.${date.getMilliseconds().toString().padStart(3, '0')}`;
          
          return (
            <div key={log.id} className="flex gap-3 hover:bg-slate-800/50 py-0.5 rounded px-1 transition-colors">
              <span className="text-slate-500 shrink-0">[{timeString}]</span>
              <span className={`shrink-0 w-10 font-bold ${getLogColor(log.type)}`}>{log.type}</span>
              <span className="text-slate-300 break-all">{log.message}</span>
            </div>
          );
        })}
        {logs.length === 0 && (
          <div className="text-slate-600 italic mt-2">Waiting for data stream...</div>
        )}
      </div>
    </div>
  );
};
