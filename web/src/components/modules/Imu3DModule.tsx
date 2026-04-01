import React, { useLayoutEffect, useMemo, useRef, Suspense } from 'react';
import type { TelemetryData } from '../../types/telemetry';
import { Canvas, useFrame } from '@react-three/fiber';
import { OrbitControls, Box, useGLTF } from '@react-three/drei';
import * as THREE from 'three';

interface Imu3DModuleProps {
  currentData: TelemetryData['imu'];
}

const MODEL_SCALE = 0.1;
const MODEL_MOUNT_ROTATION: [number, number, number] = [-Math.PI / 2, 0, -Math.PI / 2];
const MODEL_PIVOT_TRIM: [number, number, number] = [0, 0, 0];

// O sub-componentă pentru modelul 3D al plăcii (necesită un fișier esp32.glb în folderul public/)
const Esp32Model: React.FC<{ pitch: number; roll: number; yaw: number }> = ({ pitch, roll, yaw }) => {
  const imuRotationRef = useRef<THREE.Group>(null!);
  const geometryPivotRef = useRef<THREE.Group>(null!);
  
  // hook-ul useGLTF încarcă automat fișierul. Dacă vrei să revii la cutie, poți folosi SensorBox
  const { scene } = useGLTF('/esp32.glb');
  const sceneClone = useMemo(() => scene.clone(true), [scene]);

  useLayoutEffect(() => {
    if (!geometryPivotRef.current) return;

    const pivot = geometryPivotRef.current;
    pivot.clear();

    // Re-centreaza geometria modelului astfel incat pivotul local sa fie in mijlocul placii.
    const model = sceneClone;
    model.updateMatrixWorld(true);

    const bbox = new THREE.Box3().setFromObject(model);
    if (!bbox.isEmpty()) {
      const center = bbox.getCenter(new THREE.Vector3());
      model.position.sub(center);
    }

    // Ajustare fina optionala dupa auto-centrare (in unitati locale GLB).
    model.position.add(new THREE.Vector3(...MODEL_PIVOT_TRIM));

    pivot.add(model);

    return () => {
      pivot.remove(model);
    };
  }, [sceneClone]);

  useFrame(() => {
    if (!imuRotationRef.current) return;

    // Echivalentul mobil: Rx = -Y, Ry = Z, Rz = -X.
    const targetX = -pitch;
    const targetY = yaw;
    const targetZ = -roll;

    imuRotationRef.current.rotation.set(targetX, targetY, targetZ);
  });

  return (
    <group ref={imuRotationRef}>
      {/* Rotatia fixa de montaj ramane separata de rotatia live a IMU-ului. */}
      <group rotation={MODEL_MOUNT_ROTATION} scale={MODEL_SCALE}>
        <group ref={geometryPivotRef} />
      </group>
    </group>
  );
};

// Cutia simplă folosită ca Fallback sau dacă nu avem modelul încă
const SensorBox: React.FC<{ pitch: number; roll: number; yaw: number }> = ({ pitch, roll, yaw }) => {
  const meshRef = useRef<THREE.Mesh>(null!);

  useFrame(() => {
    if (!meshRef.current) return;

    // Mapeaza identic cu pipeline-ul mobil.
    meshRef.current.rotation.x = -pitch;
    meshRef.current.rotation.y = yaw;
    meshRef.current.rotation.z = -roll;
  });

  return (
    <Box ref={meshRef} args={[3, 0.4, 2]}> {/* Un dreptunghi, forma asemanatoare cu o placa ESP32 */}
      <meshStandardMaterial color="#3b82f6" roughness={0.4} metalness={0.7} />
    </Box>
  );
};

export const Imu3DModule: React.FC<Imu3DModuleProps> = ({ currentData }) => {
  const displayRoll = currentData.roll;
  const displayPitch = currentData.pitch;
  const displayYaw = currentData.yaw;
  const gyroX = currentData.gyroX ?? 0;
  const gyroY = currentData.gyroY ?? 0;
  const gyroZ = currentData.gyroZ ?? 0;
  const accelX = currentData.accelX ?? 0;
  const accelY = currentData.accelY ?? 0;
  const accelZ = currentData.accelZ ?? 0;
  const sequence = currentData.sequence ?? 0;

  return (
    <div className="bg-slate-800 rounded-xl p-6 shadow-lg border border-slate-700/50 flex flex-col h-full col-span-12">
      <div className="flex justify-between items-center mb-4">
        <h2 className="text-xl font-semibold text-slate-200">Real-Time Reality (BMI160 IMU)</h2>
        <div className="text-sm px-3 py-1 bg-slate-900 border border-slate-700 rounded-full text-slate-400">
          Accel |v|: <span className="text-white font-bold">{currentData.accel.toFixed(0)} raw</span>
        </div>
      </div>
      
      <div className="flex-1 flex gap-4 min-h-125">
        
        {/* Vizualizorul 3D */}
        <div className="flex-1 bg-slate-900 rounded-lg border border-slate-700/30 overflow-hidden relative cursor-move">
          <Canvas camera={{ position: [0, 5, 5], fov: 45 }}>
            <ambientLight intensity={0.6} />
            <directionalLight position={[10, 10, 5]} intensity={1.5} />
            <pointLight position={[-10, -10, -5]} color="#2dd4bf" intensity={0.5} />
            
            <Suspense fallback={<SensorBox pitch={displayPitch} roll={displayRoll} yaw={displayYaw} />}>
              <Esp32Model pitch={displayPitch} roll={displayRoll} yaw={displayYaw} />
            </Suspense>

            {/* Setam un grid sub placa sa arate bine spatial */}
            <gridHelper args={[20, 20, '#475569', '#1e293b']} position={[0, -2, 0]} />
            
            {/* Permite userului sa se invarta in jurul cubului cu click si glisare */}
            <OrbitControls enableZoom={true} enablePan={false} />
          </Canvas>

          {/* O sageata statica ca overlay ajutator spre "Fata/Front" */}
          <div className="absolute top-4 left-0 right-0 pointer-events-none text-center opacity-40">
             <p className="text-xs uppercase tracking-widest text-[#3b82f6] font-bold">Front Axis</p>
          </div>
        </div>

        {/* Panou Lateral de detalii Gyro */}
        <div className="w-64 bg-slate-900 rounded-lg border border-slate-700/30 p-4 flex flex-col gap-4">
           <div className="text-slate-400 text-xs font-semibold uppercase tracking-wider mb-2">IMU Stream</div>
           
           <div className="flex justify-between items-center bg-slate-950 p-2 rounded">
             <span className="text-sm text-red-400">Rot X</span>
             <span className="font-mono text-sm">{(displayRoll * (180/Math.PI)).toFixed(1)}°</span>
           </div>
           
           <div className="flex justify-between items-center bg-slate-950 p-2 rounded">
             <span className="text-sm text-green-400">Rot Y</span>
             <span className="font-mono text-sm">{(displayPitch * (180/Math.PI)).toFixed(1)}°</span>
           </div>
           
           <div className="flex justify-between items-center bg-slate-950 p-2 rounded">
             <span className="text-sm text-blue-400">Rot Z</span>
             <span className="font-mono text-sm">{(displayYaw * (180/Math.PI)).toFixed(1)}°</span>
           </div>

           <div className="bg-slate-950 p-2 rounded text-[11px] font-mono text-slate-300 leading-5">
             <div>Gyro: {gyroX}, {gyroY}, {gyroZ}</div>
             <div>Accel: {accelX}, {accelY}, {accelZ}</div>
           </div>

           <div className="bg-slate-950 p-2 rounded text-xs text-slate-400 font-mono">
             Seq: {sequence}
           </div>

           <div className="mt-auto opacity-50 text-xs text-slate-500 text-center">
             Mobile-style complementary filter rendered in WebGL via Three.js
           </div>
        </div>

      </div>
    </div>
  );
};