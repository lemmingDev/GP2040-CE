import { ChangeEvent, useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Form, FormCheck, Row, Tab, Tabs } from 'react-bootstrap';
import * as yup from 'yup';

import Section from '../Components/Section';
import FormControl from '../Components/FormControl';
import WebApi from '../Services/WebApi';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

export const gplinkAnalogScheme = {
	GPLinkAnalogEnabled: yup.number().required().label('GPLink Analog Enabled'),
	gplinkAnalogLxPin: yup
		.number()
		.label('GPLink Left X Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
	gplinkAnalogLyPin: yup
		.number()
		.label('GPLink Left Y Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
	gplinkAnalogRxPin: yup
		.number()
		.label('GPLink Right X Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
	gplinkAnalogRyPin: yup
		.number()
		.label('GPLink Right Y Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
	gplinkAnalogInnerDeadzoneEnabled: yup
		.number()
		.label('GPLink Inner Deadzone Enable')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 15),
	gplinkAnalogOuterDeadzoneEnabled: yup
		.number()
		.label('GPLink Outer Deadzone Enable')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 15),
	gplinkAnalogLeftStickDeadzoneEnabled: yup
		.number()
		.label('GPLink Left Stick Deadzone Enabled')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 1),
	gplinkAnalogRightStickDeadzoneEnabled: yup
		.number()
		.label('GPLink Right Stick Deadzone Enabled')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 1),
	gplinkAnalogAxis0InnerDeadzone: yup
		.number()
		.label('GPLink Axis 0 Inner Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 50),
	gplinkAnalogAxis1InnerDeadzone: yup
		.number()
		.label('GPLink Axis 1 Inner Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 50),
	gplinkAnalogAxis2InnerDeadzone: yup
		.number()
		.label('GPLink Axis 2 Inner Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 50),
	gplinkAnalogAxis3InnerDeadzone: yup
		.number()
		.label('GPLink Axis 3 Inner Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 50),
	gplinkAnalogAxis0OuterDeadzone: yup
		.number()
		.label('GPLink Axis 0 Outer Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 50, 100),
	gplinkAnalogAxis1OuterDeadzone: yup
		.number()
		.label('GPLink Axis 1 Outer Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 50, 100),
	gplinkAnalogAxis2OuterDeadzone: yup
		.number()
		.label('GPLink Axis 2 Outer Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 50, 100),
	gplinkAnalogAxis3OuterDeadzone: yup
		.number()
		.label('GPLink Axis 3 Outer Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 50, 100),
	gplinkAnalogLeftStickDeadzone: yup
		.number()
		.label('GPLink Left Stick Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 20),
	gplinkAnalogRightStickDeadzone: yup
		.number()
		.label('GPLink Right Stick Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 20),
	gplinkAnalogInvertEnabled: yup
		.number()
		.label('GPLink Invert Enable')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 15),
	gplinkAnalogAutoCalibrate: yup
		.number()
		.label('GPLink Auto Calibrate')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 15),
	gplinkAnalogLxCenter: yup
		.number()
		.label('GPLink Left X Center')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogLyCenter: yup
		.number()
		.label('GPLink Left Y Center')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogRxCenter: yup
		.number()
		.label('GPLink Right X Center')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogRyCenter: yup
		.number()
		.label('GPLink Right Y Center')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogSmoothingEnabled: yup
		.number()
		.label('GPLink Smoothing')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 1),
	gplinkAnalogSmoothingFactor: yup
		.number()
		.label('GPLink Smoothing Factor')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 10),
	gplinkAnalogForcedCircularity: yup
		.number()
		.label('GPLink Force Circularity')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 1),
	gplinkAnalogSmoothingEnabled2: yup
		.number()
		.label('GPLink Smoothing 2')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 1),
	gplinkAnalogSmoothingFactor2: yup
		.number()
		.label('GPLink Smoothing Factor 2')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 10),
	gplinkAnalogForcedCircularity2: yup
		.number()
		.label('GPLink Force Circularity 2')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 1),
	gplinkAnalogLtPin: yup
		.number()
		.label('GPLink Left Trigger Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
	gplinkAnalogRtPin: yup
		.number()
		.label('GPLink Right Trigger Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
	gplinkAnalogLtMin: yup
		.number()
		.label('GPLink Left Trigger Min')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogLtMax: yup
		.number()
		.label('GPLink Left Trigger Max')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535)
		.test(
			'lt-window',
			'GPLink Left Trigger Max: Max must be above Min',
			function (v) {
				if (!this.parent.GPLinkAnalogEnabled) return true;
				return v > this.parent.gplinkAnalogLtMin;
			},
		),
	gplinkAnalogRtMin: yup
		.number()
		.label('GPLink Right Trigger Min')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogRtMax: yup
		.number()
		.label('GPLink Right Trigger Max')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535)
		.test(
			'rt-window',
			'GPLink Right Trigger Max: Max must be above Min',
			function (v) {
				if (!this.parent.GPLinkAnalogEnabled) return true;
				return v > this.parent.gplinkAnalogRtMin;
			},
		),
	gplinkAnalogTriggerInvert: yup
		.number()
		.label('GPLink Trigger Invert')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 3),
	gplinkAnalogLxMin: yup
		.number()
		.label('GPLink Left X Min')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogLxMax: yup
		.number()
		.label('GPLink Left X Max')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogLyMin: yup
		.number()
		.label('GPLink Left Y Min')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogLyMax: yup
		.number()
		.label('GPLink Left Y Max')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogRxMin: yup
		.number()
		.label('GPLink Right X Min')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogRxMax: yup
		.number()
		.label('GPLink Right X Max')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogRyMin: yup
		.number()
		.label('GPLink Right Y Min')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogRyMax: yup
		.number()
		.label('GPLink Right Y Max')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
};

export const gplinkAnalogState = {
	GPLinkAnalogEnabled: 0,
	gplinkAnalogLxPin: -1,
	gplinkAnalogLyPin: -1,
	gplinkAnalogRxPin: -1,
	gplinkAnalogRyPin: -1,
	gplinkAnalogInnerDeadzoneEnabled: 0,
	gplinkAnalogOuterDeadzoneEnabled: 0,
	gplinkAnalogLeftStickDeadzoneEnabled: 0,
	gplinkAnalogRightStickDeadzoneEnabled: 0,
	gplinkAnalogAxis0InnerDeadzone: 5,
	gplinkAnalogAxis1InnerDeadzone: 5,
	gplinkAnalogAxis2InnerDeadzone: 5,
	gplinkAnalogAxis3InnerDeadzone: 5,
	gplinkAnalogAxis0OuterDeadzone: 100,
	gplinkAnalogAxis1OuterDeadzone: 100,
	gplinkAnalogAxis2OuterDeadzone: 100,
	gplinkAnalogAxis3OuterDeadzone: 100,
	gplinkAnalogLeftStickDeadzone: 5,
	gplinkAnalogRightStickDeadzone: 5,
	gplinkAnalogInvertEnabled: 0,
	gplinkAnalogAutoCalibrate: 0,
	gplinkAnalogLxCenter: 32767,
	gplinkAnalogLyCenter: 32767,
	gplinkAnalogRxCenter: 32767,
	gplinkAnalogRyCenter: 32767,
	gplinkAnalogSmoothingEnabled: 0,
	gplinkAnalogSmoothingFactor: 2,
	gplinkAnalogForcedCircularity: 0,
	gplinkAnalogSmoothingEnabled2: 0,
	gplinkAnalogSmoothingFactor2: 2,
	gplinkAnalogForcedCircularity2: 0,
	gplinkAnalogLtPin: -1,
	gplinkAnalogRtPin: -1,
	gplinkAnalogLtMin: 0,
	gplinkAnalogLtMax: 65535,
	gplinkAnalogRtMin: 0,
	gplinkAnalogRtMax: 65535,
	gplinkAnalogTriggerInvert: 0,
	gplinkAnalogLxMin: 0,
	gplinkAnalogLxMax: 65535,
	gplinkAnalogLyMin: 0,
	gplinkAnalogLyMax: 65535,
	gplinkAnalogRxMin: 0,
	gplinkAnalogRxMax: 65535,
	gplinkAnalogRyMin: 0,
	gplinkAnalogRyMax: 65535,
};

// Stick tabs mirror the core Analog page: pins + deadzones + one-click
// center capture per stick.
const STICKS = [
	{
		key: 'stick1',
		titleKey: 'AddonsConfig:gplink-analog-stick-1',
		xPin: 'gplinkAnalogLxPin',
		yPin: 'gplinkAnalogLyPin',
		xAxis: 0,
		yAxis: 1,
		deadzoneEnabled: 'gplinkAnalogLeftStickDeadzoneEnabled',
		deadzone: 'gplinkAnalogLeftStickDeadzone',
		deadzoneEnabledLabel:
			'AddonsConfig:gplink-analog-left-stick-deadzone-enabled-label',
		deadzoneLabel: 'AddonsConfig:gplink-analog-left-stick-deadzone-label',
		centerX: 'gplinkAnalogLxCenter',
		centerY: 'gplinkAnalogLyCenter',
		valueX: 'lx',
		valueY: 'ly',
		minX: 'gplinkAnalogLxMin',
		maxX: 'gplinkAnalogLxMax',
		minY: 'gplinkAnalogLyMin',
		maxY: 'gplinkAnalogLyMax',
		smoothingEnabled: 'gplinkAnalogSmoothingEnabled',
		smoothingFactor: 'gplinkAnalogSmoothingFactor',
		forcedCircularity: 'gplinkAnalogForcedCircularity',
	},
	{
		key: 'stick2',
		titleKey: 'AddonsConfig:gplink-analog-stick-2',
		xPin: 'gplinkAnalogRxPin',
		yPin: 'gplinkAnalogRyPin',
		xAxis: 2,
		yAxis: 3,
		deadzoneEnabled: 'gplinkAnalogRightStickDeadzoneEnabled',
		deadzone: 'gplinkAnalogRightStickDeadzone',
		deadzoneEnabledLabel:
			'AddonsConfig:gplink-analog-right-stick-deadzone-enabled-label',
		deadzoneLabel: 'AddonsConfig:gplink-analog-right-stick-deadzone-label',
		centerX: 'gplinkAnalogRxCenter',
		centerY: 'gplinkAnalogRyCenter',
		valueX: 'rx',
		valueY: 'ry',
		minX: 'gplinkAnalogRxMin',
		maxX: 'gplinkAnalogRxMax',
		minY: 'gplinkAnalogRyMin',
		maxY: 'gplinkAnalogRyMax',
		smoothingEnabled: 'gplinkAnalogSmoothingEnabled2',
		smoothingFactor: 'gplinkAnalogSmoothingFactor2',
		forcedCircularity: 'gplinkAnalogForcedCircularity2',
	},
];

// Trigger rows mirror the stick center capture: pin + calibration window
// with one-click rest/full capture from the live raw value.
const TRIGGERS = [
	{
		key: 'lt',
		name: 'LT',
		bit: 1,
		pin: 'gplinkAnalogLtPin',
		min: 'gplinkAnalogLtMin',
		max: 'gplinkAnalogLtMax',
		pinLabel: 'AddonsConfig:gplink-analog-lt-pin-label',
	},
	{
		key: 'rt',
		name: 'RT',
		bit: 2,
		pin: 'gplinkAnalogRtPin',
		min: 'gplinkAnalogRtMin',
		max: 'gplinkAnalogRtMax',
		pinLabel: 'AddonsConfig:gplink-analog-rt-pin-label',
	},
];

// Deadzone slider (0-20%): label with live value, range input, inline error.
// Used for every deadzone percent field instead of a number box.
const DeadzoneSlider = ({
	label,
	name,
	value,
	error,
	isInvalid,
	onChange,
	groupClassName,
	hidden,
	min = 0,
	max = 20,
}: {
	label: string;
	name: string;
	value: number;
	error?: string;
	isInvalid?: boolean;
	onChange: (e: ChangeEvent<HTMLInputElement>) => void;
	groupClassName?: string;
	hidden?: boolean;
	min?: number;
	max?: number;
}) => (
	<div className={groupClassName} hidden={hidden}>
		<Form.Label htmlFor={name}>
			{label}{' '}
			<span className="text-muted" style={{ fontVariantNumeric: 'tabular-nums' }}>
				{value}%
			</span>
		</Form.Label>
		<Form.Range
			id={name}
			name={name}
			value={value}
			onChange={onChange}
			min={min}
			max={max}
			step={1}
			isInvalid={isInvalid}
		/>
		{isInvalid === true && error != null && error !== '' && (
			<div className="text-danger">
				<small>{error}</small>
			</div>
		)}
	</div>
);

// Active tab cache, module-level so a save (which unmounts the section
// behind the loading spinner and remounts it) returns to the tab you were
// on instead of falling back to stick1.
let gplinkAnalogActiveTab = 'stick1';

// 2-D stick position pad: dot at the live raw value over a center crosshair.
// Decorative (aria-hidden); the adjacent live text carries the values. Drawn
// as inline SVG so dot and crosshair share one coordinate system and coincide
// exactly (separate CSS-positioned divs can round apart by a pixel).
const StickPad = ({ x, y }: { x: number; y: number }) => {
	const cx = 4 + Math.min(1, Math.max(0, x / 65535)) * 88;
	const cy = 4 + Math.min(1, Math.max(0, y / 65535)) * 88;
	return (
		<svg
			width={96}
			height={96}
			aria-hidden="true"
			style={{
				flexShrink: 0,
				border: '1px solid var(--bs-border-color)',
				borderRadius: 4,
			}}
		>
			<line
				x1={48}
				y1={6}
				x2={48}
				y2={90}
				style={{ stroke: 'var(--bs-border-color)', strokeWidth: 1 }}
			/>
			<line
				x1={6}
				y1={48}
				x2={90}
				y2={48}
				style={{ stroke: 'var(--bs-border-color)', strokeWidth: 1 }}
			/>
			<circle cx={cx} cy={cy} r={4} style={{ fill: 'var(--bs-primary)' }} />
		</svg>
	);
};

// Trigger level bar: shaded [min, max] window with a primary marker at the
// live raw value and a success marker at the shaped output (0-255). Decorative
// (aria-hidden); the adjacent live text carries the values.
const TriggerBar = ({
	v,
	s,
	min,
	max,
}: {
	v: number;
	s: number;
	min: number;
	max: number;
}) => {
	const pct = (n: number) => Math.min(100, Math.max(0, (n / 65535) * 100));
	const lo = Math.min(min, max);
	const hi = Math.max(min, max);
	return (
		<div
			aria-hidden="true"
			className="mt-1"
			style={{
				position: 'relative',
				height: 10,
				background: 'var(--bs-tertiary-bg)',
				borderRadius: 4,
				overflow: 'hidden',
			}}
		>
			<div
				style={{
					position: 'absolute',
					left: `${pct(lo)}%`,
					width: `${Math.max(0, pct(hi) - pct(lo))}%`,
					top: 0,
					bottom: 0,
					background: 'var(--bs-secondary)',
				}}
			/>
			<div
				style={{
					position: 'absolute',
					left: `calc(${pct(v)}% - 4px)`,
					top: 0,
					bottom: 0,
					width: 8,
					background: 'var(--bs-primary)',
				}}
			/>
			<div
				style={{
					position: 'absolute',
					left: `calc(${Math.min(100, Math.max(0, (s / 255) * 100))}% - 4px)`,
					top: 0,
					bottom: 0,
					width: 8,
					background: 'var(--bs-success)',
				}}
			/>
		</div>
	);
};

const GPLinkAnalog = ({
	values,
	errors,
	handleChange,
	handleCheckbox,
	setFieldValue,
}: AddonPropTypes) => {
	const { t } = useTranslation();

	// Live raw companion values for debugging/calibration, polled while the
	// page is open. Keys match /api/getGPLinkAnalogValues (lx/ly/rx/ry/lt/rt).
	const [liveValues, setLiveValues] = useState<Record<string, number>>({
		lx: 32767,
		ly: 32767,
		rx: 32767,
		ry: 32767,
		lt: 0,
		rt: 0,
		lxS: 32767,
		lyS: 32767,
		rxS: 32767,
		ryS: 32767,
		ltS: 0,
		rtS: 0,
	});
	const [activeTab, setActiveTab] = useState(gplinkAnalogActiveTab);
	// False when the live feed errors: surfaces poll failures in the UI
	// instead of leaving silently frozen numbers.
	const [liveOk, setLiveOk] = useState(true);
	useEffect(() => {
		let alive = true;
		let id: ReturnType<typeof setInterval> | null = null;
		const fetchLive = async () => {
			try {
				const data = await WebApi.getGPLinkAnalogValues();
				if (!alive) return;
				// The lx key is always written; its absence means the payload
				// serialized empty (e.g. doc pool overflow), not just stale.
				if (!data || typeof data.lx !== 'number') {
					setLiveOk(false);
					return;
				}
				setLiveOk(true);
				setLiveValues((prev) => {
					// Bail out (same reference, no re-render) when nothing moved:
					// the companion only pushes on change, so most polls repeat.
					let changed = false;
					const next = { ...prev };
					for (const k of [
						'lx',
						'ly',
						'rx',
						'ry',
						'lt',
						'rt',
						'lxS',
						'lyS',
						'rxS',
						'ryS',
						'ltS',
						'rtS',
					] as const) {
						if (typeof data[k] === 'number' && data[k] !== prev[k]) {
							next[k] = data[k];
							changed = true;
						}
					}
					return changed ? next : prev;
				});
			} catch {
				if (alive) setLiveOk(false);
			}
		};
		const start = () => {
			fetchLive();
			// 10 Hz: the companion pushes on change, so this is display latency
			// only. Re-renders are skipped when values repeat (see above), so
			// idle cost is one small GET per tick on core1's HTTP server.
			if (id === null) id = setInterval(fetchLive, 100);
		};
		const stop = () => {
			if (id !== null) {
				clearInterval(id);
				id = null;
			}
		};
		// Pause polling while the browser tab is hidden; resume on return.
		const onVisibility = () => {
			if (document.hidden) stop();
			else if (alive) start();
		};
		document.addEventListener('visibilitychange', onVisibility);
		if (!document.hidden) start();
		return () => {
			alive = false;
			stop();
			document.removeEventListener('visibilitychange', onVisibility);
		};
	}, []);

	const calibrateStick = async (stick: (typeof STICKS)[number]) => {
		const data = await WebApi.getGPLinkAnalogValues();
		if (!data) return;
		if (typeof data[stick.valueX] === 'number') {
			setFieldValue(stick.centerX, data[stick.valueX]);
		}
		if (typeof data[stick.valueY] === 'number') {
			setFieldValue(stick.centerY, data[stick.valueY]);
		}
	};

	// Capture a trigger window edge from the live value with a ±1% margin so
	// noise can't peek over the edges. Cross-clamped to keep min < max.
	const captureTrigger = async (
		valueKey: 'lt' | 'rt',
		minField: string,
		maxField: string,
		isRest: boolean,
	) => {
		const data = await WebApi.getGPLinkAnalogValues();
		if (!data || typeof data[valueKey] !== 'number') return;
		const sample = data[valueKey] as number;
		const margin = 655;
		if (isRest) {
			setFieldValue(minField, Math.min(sample + margin, values[maxField] - 1));
		} else {
			setFieldValue(maxField, Math.max(sample - margin, values[minField] + 1));
		}
	};

	// Capture a stick axis window edge from the live raw value (exact sample,
	// no margin — mirrors official readCalibrationSample averaging intent;
	// the user trims with the fields).
	const captureAxis = async (
		valueKey: 'lx' | 'ly' | 'rx' | 'ry',
		field: string,
	) => {
		const data = await WebApi.getGPLinkAnalogValues();
		if (!data || typeof data[valueKey] !== 'number') return;
		setFieldValue(field, data[valueKey] as number);
	};

	return (
		<Section title={t('AddonsConfig:gplink-analog-header-text')}>
			<div id="GPLinkAnalogOptions" hidden={!values.GPLinkAnalogEnabled}>
				{/* One banner slot above the tabs, swapped per tab so the
				stick and trigger guidance never stack. */}
				{activeTab === 'triggers' ? (
					<div className="alert alert-info" role="alert">
						{t('AddonsConfig:gplink-analog-triggers-sub-header-text')}
					</div>
				) : (
					<div className="alert alert-info" role="alert">
						{t('AddonsConfig:gplink-analog-sub-header-text')}
					</div>
				)}
				<Tabs
					activeKey={activeTab}
					onSelect={(k) => {
						if (k) {
							gplinkAnalogActiveTab = k;
							setActiveTab(k);
						}
					}}
					id="gplinkAnalogTabs"
					className="mb-3 pb-0"
					fill
				>
					{STICKS.map((stick) => (
						<Tab
							key={stick.key}
							eventKey={stick.key}
							title={t(stick.titleKey)}
						>
							<Row className="mb-3">
								<div className="col-sm-7">
									<Row className="mb-3">
										<FormControl
											type="number"
											label={t(
												`AddonsConfig:gplink-analog-${stick.key === 'stick1' ? 'lx' : 'rx'}-pin-label`,
											)}
									name={stick.xPin}
									className="form-control-sm"
									groupClassName="col-sm-6 mb-3"
									value={values[stick.xPin]}
									error={errors[stick.xPin]}
									isInvalid={Boolean(errors[stick.xPin])}
									onChange={handleChange}
									min={-1}
									max={63}
								/>
								<FormControl
									type="number"
									label={t(
										`AddonsConfig:gplink-analog-${stick.key === 'stick1' ? 'ly' : 'ry'}-pin-label`,
									)}
									name={stick.yPin}
									className="form-control-sm"
									groupClassName="col-sm-6 mb-3"
									value={values[stick.yPin]}
									error={errors[stick.yPin]}
									isInvalid={Boolean(errors[stick.yPin])}
									onChange={handleChange}
									min={-1}
									max={63}
								/>
							</Row>
							<Row className="mb-3">
								{[stick.xAxis, stick.yAxis].map((axis) => {
									const bit = 8 >> axis;
									const label = (
										axis === stick.xAxis ? stick.valueX : stick.valueY
									).toUpperCase();
									return (
										<FormCheck
											key={`${stick.key}-invert-${axis}`}
											label={t(
												'AddonsConfig:gplink-analog-axis-invert-label',
												{ axis: label },
											)}
											type="switch"
											id={`GPLinkAnalog${stick.key}Invert${axis}`}
											className="col-sm-5 ms-3"
											isInvalid={false}
											checked={Boolean(values.gplinkAnalogInvertEnabled & bit)}
											onChange={() =>
												setFieldValue(
													'gplinkAnalogInvertEnabled',
													values.gplinkAnalogInvertEnabled ^ bit,
												)
											}
										/>
									);
								})}
							</Row>
							<Row className="mb-3">
								<div className="col-sm-12">
									<Button size="sm" onClick={() => calibrateStick(stick)}>
										{t('AddonsConfig:gplink-analog-calibrate-label')}
									</Button>{' '}
									<span className="text-muted">
										{t('AddonsConfig:gplink-analog-center-text', {
											x: values[stick.centerX],
											y: values[stick.centerY],
										})}
									</span>{' '}
									<span className="text-muted">
										{t('AddonsConfig:gplink-analog-calibrate-help-text')}
									</span>
								</div>
							</Row>
							<Row className="mb-3">
								<div className="col-sm-12 d-flex align-items-center gap-3">
									<div>
										<StickPad
											x={liveValues[stick.valueX]}
											y={liveValues[stick.valueY]}
										/>
										<div>
											<small className="text-muted">
												{t('AddonsConfig:gplink-analog-raw-label')}
											</small>
										</div>
									</div>
									<div>
										<StickPad
											x={liveValues[`${stick.valueX}S`]}
											y={liveValues[`${stick.valueY}S`]}
										/>
										<div>
											<small className="text-muted">
												{t('AddonsConfig:gplink-analog-shaped-label')}
											</small>
										</div>
									</div>
									<div>
										<span
											className="text-muted"
											aria-live="polite"
											style={{ fontVariantNumeric: 'tabular-nums' }}
										>
											{t('AddonsConfig:gplink-analog-live-text', {
												x: liveValues[stick.valueX],
												y: liveValues[stick.valueY],
											})}
										</span>
										<br />
										<span
											className="text-muted"
											style={{ fontVariantNumeric: 'tabular-nums' }}
										>
											{t('AddonsConfig:gplink-analog-shaped-text', {
												x: liveValues[`${stick.valueX}S`],
												y: liveValues[`${stick.valueY}S`],
											})}
										</span>
										{!liveOk && (
											<span className="text-danger">
												{' '}
												{t('AddonsConfig:gplink-analog-live-error-text')}
											</span>
										)}
									</div>
								</div>
							</Row>
							<Row className="mb-3">
								<FormControl
									type="number"
									label={t('AddonsConfig:gplink-analog-axis-min-label', {
										axis: stick.key === 'stick1' ? 'LX' : 'RX',
									})}
									name={stick.minX}
									className="form-control-sm"
									groupClassName="col-sm-6 mb-3"
									value={values[stick.minX]}
									error={errors[stick.minX]}
									isInvalid={Boolean(errors[stick.minX])}
									onChange={handleChange}
									min={0}
									max={65535}
								/>
								<FormControl
									type="number"
									label={t('AddonsConfig:gplink-analog-axis-max-label', {
										axis: stick.key === 'stick1' ? 'LX' : 'RX',
									})}
									name={stick.maxX}
									className="form-control-sm"
									groupClassName="col-sm-6 mb-3"
									value={values[stick.maxX]}
									error={errors[stick.maxX]}
									isInvalid={Boolean(errors[stick.maxX])}
									onChange={handleChange}
									min={0}
									max={65535}
								/>
							</Row>
							<Row className="mb-3">
								<FormControl
									type="number"
									label={t('AddonsConfig:gplink-analog-axis-min-label', {
										axis: stick.key === 'stick1' ? 'LY' : 'RY',
									})}
									name={stick.minY}
									className="form-control-sm"
									groupClassName="col-sm-6 mb-3"
									value={values[stick.minY]}
									error={errors[stick.minY]}
									isInvalid={Boolean(errors[stick.minY])}
									onChange={handleChange}
									min={0}
									max={65535}
								/>
								<FormControl
									type="number"
									label={t('AddonsConfig:gplink-analog-axis-max-label', {
										axis: stick.key === 'stick1' ? 'LY' : 'RY',
									})}
									name={stick.maxY}
									className="form-control-sm"
									groupClassName="col-sm-6 mb-3"
									value={values[stick.maxY]}
									error={errors[stick.maxY]}
									isInvalid={Boolean(errors[stick.maxY])}
									onChange={handleChange}
									min={0}
									max={65535}
								/>
							</Row>
							<Row className="mb-3">
								<div className="col-sm-12">
									<Button
										size="sm"
										onClick={() =>
											captureAxis(
												stick.valueX as 'lx' | 'ly' | 'rx' | 'ry',
												stick.minX,
											)
										}
									>
										{t('AddonsConfig:gplink-analog-set-min-label', {
											axis: stick.key === 'stick1' ? 'LX' : 'RX',
										})}
									</Button>{' '}
									<Button
										size="sm"
										onClick={() =>
											captureAxis(
												stick.valueX as 'lx' | 'ly' | 'rx' | 'ry',
												stick.maxX,
											)
										}
									>
										{t('AddonsConfig:gplink-analog-set-max-label', {
											axis: stick.key === 'stick1' ? 'LX' : 'RX',
										})}
									</Button>{' '}
									<Button
										size="sm"
										onClick={() =>
											captureAxis(
												stick.valueY as 'lx' | 'ly' | 'rx' | 'ry',
												stick.minY,
											)
										}
									>
										{t('AddonsConfig:gplink-analog-set-min-label', {
											axis: stick.key === 'stick1' ? 'LY' : 'RY',
										})}
									</Button>{' '}
									<Button
										size="sm"
										onClick={() =>
											captureAxis(
												stick.valueY as 'lx' | 'ly' | 'rx' | 'ry',
												stick.maxY,
											)
										}
									>
										{t('AddonsConfig:gplink-analog-set-max-label', {
											axis: stick.key === 'stick1' ? 'LY' : 'RY',
										})}
									</Button>
								</div>
							</Row>
							<Row className="mb-3">
								<FormCheck
									label={
										<>
											{t('AddonsConfig:analog-smoothing')}{' '}
											<span
												className="text-muted"
												style={{ fontVariantNumeric: 'tabular-nums' }}
											>
												{t('AddonsConfig:gplink-analog-smoothing-factor-text', {
													v: values[stick.smoothingFactor],
												})}
											</span>
										</>
									}
									type="switch"
									id={`GPLinkAnalog${stick.key}Smoothing`}
									className="col-sm-5 ms-3"
									isInvalid={false}
									checked={Boolean(values[stick.smoothingEnabled])}
									onChange={(e) => {
										handleCheckbox(stick.smoothingEnabled);
										handleChange(e);
									}}
								/>
								<div className="col-sm-6 mb-3">
									<Form.Range
										id={stick.smoothingFactor}
										name={stick.smoothingFactor}
										aria-label={t('AddonsConfig:smoothing-factor')}
										value={values[stick.smoothingFactor]}
										onChange={handleChange}
										min={0}
										max={10}
										step={1}
									/>
									{Boolean(errors[stick.smoothingFactor]) && (
										<div className="text-danger">
											<small>{errors[stick.smoothingFactor]}</small>
										</div>
									)}
								</div>
							</Row>
							<Row className="mb-3">
								<FormCheck
									label={
										<>
											{t(stick.deadzoneEnabledLabel)}{' '}
											<span
												className="text-muted"
												style={{ fontVariantNumeric: 'tabular-nums' }}
											>
												({values[stick.deadzone]}%)
											</span>
										</>
									}
									type="switch"
									id={`GPLinkAnalog${stick.key}Deadzone`}
									className="col-sm-5 ms-3"
									isInvalid={false}
									checked={Boolean(values[stick.deadzoneEnabled])}
									onChange={(e) => {
										handleCheckbox(stick.deadzoneEnabled);
										handleChange(e);
									}}
								/>
								<div className="col-sm-6 mb-3">
									<Form.Range
										id={stick.deadzone}
										name={stick.deadzone}
										aria-label={t(stick.deadzoneLabel)}
										value={values[stick.deadzone]}
										onChange={handleChange}
										min={0}
										max={20}
										step={1}
									/>
									{Boolean(errors[stick.deadzone]) && (
										<div className="text-danger">
											<small>{errors[stick.deadzone]}</small>
										</div>
									)}
								</div>
							</Row>
							<Row className="mb-3">
								<FormCheck
									label={t('AddonsConfig:analog-force-circularity')}
									type="switch"
									id={`GPLinkAnalog${stick.key}Circularity`}
									className="col-sm-6 ms-3"
									isInvalid={false}
									checked={Boolean(values[stick.forcedCircularity])}
									onChange={(e) => {
										handleCheckbox(stick.forcedCircularity);
										handleChange(e);
									}}
								/>
							</Row>
							</div>
							<div className="col-sm-5">
								<h6>{t('AddonsConfig:gplink-analog-deadzone-header-text')}</h6>
								<DeadzoneSlider
									label={t(
										'AddonsConfig:gplink-analog-axis-inner-deadzone-label',
										{ axis: stick.key === 'stick1' ? 'LX' : 'RX' },
									)}
									name={`gplinkAnalogAxis${stick.xAxis}InnerDeadzone`}
									groupClassName="mb-3"
									max={50}
									value={values[`gplinkAnalogAxis${stick.xAxis}InnerDeadzone`]}
									error={errors[`gplinkAnalogAxis${stick.xAxis}InnerDeadzone`]}
									isInvalid={Boolean(
										errors[`gplinkAnalogAxis${stick.xAxis}InnerDeadzone`],
									)}
									onChange={handleChange}
								/>
								<DeadzoneSlider
									label={t(
										'AddonsConfig:gplink-analog-axis-inner-deadzone-label',
										{ axis: stick.key === 'stick1' ? 'LY' : 'RY' },
									)}
									name={`gplinkAnalogAxis${stick.yAxis}InnerDeadzone`}
									groupClassName="mb-3"
									max={50}
									value={values[`gplinkAnalogAxis${stick.yAxis}InnerDeadzone`]}
									error={errors[`gplinkAnalogAxis${stick.yAxis}InnerDeadzone`]}
									isInvalid={Boolean(
										errors[`gplinkAnalogAxis${stick.yAxis}InnerDeadzone`],
									)}
									onChange={handleChange}
								/>
								<DeadzoneSlider
									label={t(
										'AddonsConfig:gplink-analog-axis-outer-deadzone-label',
										{ axis: stick.key === 'stick1' ? 'LX' : 'RX' },
									)}
									name={`gplinkAnalogAxis${stick.xAxis}OuterDeadzone`}
									groupClassName="mb-3"
									min={50}
									max={100}
									value={values[`gplinkAnalogAxis${stick.xAxis}OuterDeadzone`]}
									error={errors[`gplinkAnalogAxis${stick.xAxis}OuterDeadzone`]}
									isInvalid={Boolean(
										errors[`gplinkAnalogAxis${stick.xAxis}OuterDeadzone`],
									)}
									onChange={handleChange}
								/>
								<DeadzoneSlider
									label={t(
										'AddonsConfig:gplink-analog-axis-outer-deadzone-label',
										{ axis: stick.key === 'stick1' ? 'LY' : 'RY' },
									)}
									name={`gplinkAnalogAxis${stick.yAxis}OuterDeadzone`}
									groupClassName="mb-3"
									min={50}
									max={100}
									value={values[`gplinkAnalogAxis${stick.yAxis}OuterDeadzone`]}
									error={errors[`gplinkAnalogAxis${stick.yAxis}OuterDeadzone`]}
									isInvalid={Boolean(
										errors[`gplinkAnalogAxis${stick.yAxis}OuterDeadzone`],
									)}
									onChange={handleChange}
								/>
							</div>
						</Row>
						</Tab>
					))}
					<Tab eventKey="triggers" title={t('AddonsConfig:gplink-analog-triggers')}>
						{/* Floor the tab height so the card doesn't shrink vs the
						taller stick tabs (grows past this if content needs it). */}
						<div style={{ minHeight: 470 }}>
							<div className="text-end mb-2">
								<span className="text-muted">
									<small>
										{t('AddonsConfig:gplink-analog-triggers-help-text')}
									</small>
								</span>
							</div>
							{TRIGGERS.map((trigger) => (
							<Row className="mb-3" key={trigger.key}>
								<FormControl
									type="number"
									label={t(trigger.pinLabel)}
									name={trigger.pin}
									className="form-control-sm"
									groupClassName="col-sm-3 mb-3"
									value={values[trigger.pin]}
									error={errors[trigger.pin]}
									isInvalid={Boolean(errors[trigger.pin])}
									onChange={handleChange}
									min={-1}
									max={63}
								/>
								<FormControl
									type="number"
									label={t('AddonsConfig:gplink-analog-trigger-min-label', {
										name: trigger.name,
									})}
									name={trigger.min}
									className="form-control-sm"
									groupClassName="col-sm-2 mb-3"
									value={values[trigger.min]}
									error={errors[trigger.min]}
									isInvalid={Boolean(errors[trigger.min])}
									onChange={handleChange}
									min={0}
									max={65535}
								/>
								<FormControl
									type="number"
									label={t('AddonsConfig:gplink-analog-trigger-max-label', {
										name: trigger.name,
									})}
									name={trigger.max}
									className="form-control-sm"
									groupClassName="col-sm-2 mb-3"
									value={values[trigger.max]}
									error={errors[trigger.max]}
									isInvalid={Boolean(errors[trigger.max])}
									onChange={handleChange}
									min={0}
									max={65535}
								/>
								<div className="col-sm-5">
									<FormCheck
										label={t('AddonsConfig:gplink-analog-axis-invert-label', {
											axis: trigger.name,
										})}
										type="switch"
										id={`GPLinkAnalog${trigger.key}Invert`}
										className="mb-2 ms-3"
										isInvalid={false}
										checked={Boolean(values.gplinkAnalogTriggerInvert & trigger.bit)}
										onChange={() =>
											setFieldValue(
												'gplinkAnalogTriggerInvert',
												values.gplinkAnalogTriggerInvert ^ trigger.bit,
											)
										}
									/>
									<Button
										size="sm"
										onClick={() =>
											captureTrigger(
												trigger.key as 'lt' | 'rt',
												trigger.min,
												trigger.max,
												true,
											)
										}
									>
										{t('AddonsConfig:gplink-analog-trigger-rest-label')}
									</Button>{' '}
									<Button
										size="sm"
										onClick={() =>
											captureTrigger(
												trigger.key as 'lt' | 'rt',
												trigger.min,
												trigger.max,
												false,
											)
										}
									>
										{t('AddonsConfig:gplink-analog-trigger-full-label')}
									</Button>{' '}
								</div>
								<div className="col-sm-12 mt-2">
									<span
										className="text-muted"
										aria-live="polite"
										style={{ fontVariantNumeric: 'tabular-nums' }}
									>
										{t('AddonsConfig:gplink-analog-trigger-live-text', {
											v: liveValues[trigger.key],
											s: liveValues[`${trigger.key}S`],
										})}
									</span>
									{!liveOk && (
										<span className="text-danger">
											{' '}
											{t('AddonsConfig:gplink-analog-live-error-text')}
										</span>
									)}
									<TriggerBar
										v={liveValues[trigger.key]}
										s={liveValues[`${trigger.key}S`]}
										min={values[trigger.min]}
										max={values[trigger.max]}
									/>
								</div>
							</Row>
						))}
						</div>
					</Tab>
				</Tabs>
			</div>
			<Row className="mt-2 align-items-center">
				<div className="col-sm-6 d-flex align-items-center">
					<Button type="submit">
						{t('AddonsConfig:gplink-analog-save-label')}
					</Button>
				</div>
				<div className="col-sm-6 d-flex flex-column align-items-end gap-2">
					<FormCheck
						label={t('Common:switch-enabled')}
						type="switch"
						id="GPLinkAnalogButton"
						reverse
						isInvalid={false}
						checked={Boolean(values.GPLinkAnalogEnabled)}
						onChange={(e) => {
							handleCheckbox('GPLinkAnalogEnabled');
							handleChange(e);
						}}
					/>
				</div>
			</Row>
		</Section>
	);
};

export default GPLinkAnalog;
