import React from 'react';
import { AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';
import type { TelemetryData } from '../../types/telemetry';

interface RollingChartProps {
  dataHistory: TelemetryData[];
  dataKeyExtractor: (data: TelemetryData) => number;
  color: string;
  label: string;
  unit: string;
  type?: 'monotone' | 'step';
  domain?: [number | string, number | string];
}

export const RollingChart: React.FC<RollingChartProps> = ({
  dataHistory,
  dataKeyExtractor,
  color,
  label,
  unit,
  type = 'monotone',
  domain = ['auto', 'auto']
}) => {
  // Create a fixed 60-element circular buffer mapping. 
  // 0 represents right now, -59 is 60 seconds ago.
  // When application starts, it fills unreceived data with 'null' so the real line crawls in from the right.
  const chartData = Array.from({ length: 60 }).map((_, i) => {
    const historyIndex = dataHistory.length - 60 + i;
    const value = historyIndex >= 0 ? dataKeyExtractor(dataHistory[historyIndex]) : null;
    return { 
      time: i - 59, // Ends exactly at 0 (Now)
      value
    };
  });

  // Unique ID for the SVG gradient so they don't overwrite each other
  const gradientId = `grad-${label.replace(/\s+/g, '')}`;

  return (
    <ResponsiveContainer width="100%" height="100%">
      <AreaChart data={chartData} margin={{ top: 5, right: 0, left: -20, bottom: 0 }}>
        <defs>
          <linearGradient id={gradientId} x1="0" y1="0" x2="0" y2="1">
            <stop offset="5%" stopColor={color} stopOpacity={0.25}/>
            <stop offset="95%" stopColor={color} stopOpacity={0}/>
          </linearGradient>
        </defs>
        
        {/* Faint blueprint style dashed grid lines */}
        <CartesianGrid strokeDasharray="3 3" stroke="#ffffff10" vertical={true} horizontal={true} />
        
        <XAxis 
          dataKey="time" 
          type="number"
          domain={[-59, 0]}
          tickCount={5}
          stroke="#64748b" 
          fontSize={10}
          tickMargin={5}
          tickFormatter={(val) => val === 0 ? 'Now' : `${val}s`}
        />
        <YAxis 
          domain={domain} 
          hide 
        />
        
        <Tooltip 
          contentStyle={{ backgroundColor: '#0f172a', border: '1px solid #334155', borderRadius: '8px' }}
          itemStyle={{ fontSize: '12px', color, fontWeight: 'bold' }}
          labelStyle={{ fontSize: '12px', color: '#94a3b8' }}
          labelFormatter={(val) => val === 0 ? 'Right now' : `${Math.abs(Number(val))} seconds ago`}
          formatter={(value: any) => {
            if (value === undefined || value === null) return ['No Data', label];
            return [`${value} ${unit}`, label];
          }}
          isAnimationActive={false}
        />
        
        <Area 
          type={type} 
          dataKey="value" 
          stroke={color} 
          fillOpacity={1} 
          fill={`url(#${gradientId})`} 
          strokeWidth={2} 
          isAnimationActive={false}
          connectNulls={false} // Ensure it doesn't draw bizarre lines to 0
        />
      </AreaChart>
    </ResponsiveContainer>
  );
};
