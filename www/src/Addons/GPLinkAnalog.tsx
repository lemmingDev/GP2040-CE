import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, FormCheck, Row, Tab, Tabs } from 'react-bootstrap';
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
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
	gplinkAnalogAxis1InnerDeadzone: yup
		.number()
		.label('GPLink Axis 1 Inner Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
	gplinkAnalogAxis2InnerDeadzone: yup
		.number()
		.label('GPLink Axis 2 Inner Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
	gplinkAnalogAxis3InnerDeadzone: yup
		.number()
		.label('GPLink Axis 3 Inner Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
	gplinkAnalogAxis0OuterDeadzone: yup
		.number()
		.label('GPLink Axis 0 Outer Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
	gplinkAnalogAxis1OuterDeadzone: yup
		.number()
		.label('GPLink Axis 1 Outer Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
	gplinkAnalogAxis2OuterDeadzone: yup
		.number()
		.label('GPLink Axis 2 Outer Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
	gplinkAnalogAxis3OuterDeadzone: yup
		.number()
		.label('GPLink Axis 3 Outer Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
	gplinkAnalogLeftStickDeadzone: yup
		.number()
		.label('GPLink Left Stick Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
	gplinkAnalogRightStickDeadzone: yup
		.number()
		.label('GPLink Right Stick Deadzone')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
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
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
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
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 100),
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
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogRtMin: yup
		.number()
		.label('GPLink Right Trigger Min')
		.validateRangeWhenValue('GPLinkAnalogEnabled', 0, 65535),
	gplinkAnalogRtMax: yup
		.number()
		.label('GPLink Right Trigger Max')
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
	gplinkAnalogAxis0InnerDeadzone: 0,
	gplinkAnalogAxis1InnerDeadzone: 0,
	gplinkAnalogAxis2InnerDeadzone: 0,
	gplinkAnalogAxis3InnerDeadzone: 0,
	gplinkAnalogAxis0OuterDeadzone: 0,
	gplinkAnalogAxis1OuterDeadzone: 0,
	gplinkAnalogAxis2OuterDeadzone: 0,
	gplinkAnalogAxis3OuterDeadzone: 0,
	gplinkAnalogLeftStickDeadzone: 0,
	gplinkAnalogRightStickDeadzone: 0,
	gplinkAnalogInvertEnabled: 0,
	gplinkAnalogAutoCalibrate: 0,
	gplinkAnalogLxCenter: 32767,
	gplinkAnalogLyCenter: 32767,
	gplinkAnalogRxCenter: 32767,
	gplinkAnalogRyCenter: 32767,
	gplinkAnalogSmoothingEnabled: 0,
	gplinkAnalogSmoothingFactor: 5,
	gplinkAnalogForcedCircularity: 0,
	gplinkAnalogSmoothingEnabled2: 0,
	gplinkAnalogSmoothingFactor2: 5,
	gplinkAnalogForcedCircularity2: 0,
	gplinkAnalogLtPin: -1,
	gplinkAnalogRtPin: -1,
	gplinkAnalogLtMin: 0,
	gplinkAnalogLtMax: 65535,
	gplinkAnalogRtMin: 0,
	gplinkAnalogRtMax: 65535,
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
		pin: 'gplinkAnalogLtPin',
		min: 'gplinkAnalogLtMin',
		max: 'gplinkAnalogLtMax',
		pinLabel: 'AddonsConfig:gplink-analog-lt-pin-label',
	},
	{
		key: 'rt',
		name: 'RT',
		pin: 'gplinkAnalogRtPin',
		min: 'gplinkAnalogRtMin',
		max: 'gplinkAnalogRtMax',
		pinLabel: 'AddonsConfig:gplink-analog-rt-pin-label',
	},
];

// Active tab cache, module-level so a save (which unmounts the section
// behind the loading spinner and remounts it) returns to the tab you were
// on instead of falling back to stick1.
let gplinkAnalogActiveTab = 'stick1';

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
	});
	const [activeTab, setActiveTab] = useState(gplinkAnalogActiveTab);
	useEffect(() => {
		let alive = true;
		let id: ReturnType<typeof setInterval> | null = null;
		const fetchLive = async () => {
			const data = await WebApi.getGPLinkAnalogValues();
			if (alive && data) {
				setLiveValues((prev) => {
					const next = { ...prev };
					for (const k of ['lx', 'ly', 'rx', 'ry', 'lt', 'rt'] as const) {
						if (typeof data[k] === 'number') next[k] = data[k];
					}
					return next;
				});
			}
		};
		const start = () => {
			fetchLive();
			if (id === null) id = setInterval(fetchLive, 500);
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

	return (
		<Section title={t('AddonsConfig:gplink-analog-header-text')}>
			<div id="GPLinkAnalogOptions" hidden={!values.GPLinkAnalogEnabled}>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:gplink-analog-sub-header-text')}
				</div>
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
								<FormControl
									type="number"
									label={t(
										`AddonsConfig:gplink-analog-${stick.key === 'stick1' ? 'lx' : 'rx'}-pin-label`,
									)}
									name={stick.xPin}
									className="form-control-sm"
									groupClassName="col-sm-3 mb-3"
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
									groupClassName="col-sm-3 mb-3"
									value={values[stick.yPin]}
									error={errors[stick.yPin]}
									isInvalid={Boolean(errors[stick.yPin])}
									onChange={handleChange}
									min={-1}
									max={63}
								/>
								<FormControl
									type="number"
									label={t(
										'AddonsConfig:gplink-analog-axis-inner-deadzone-label',
										{ axis: stick.key === 'stick1' ? 'LX' : 'RX' },
									)}
									name={`gplinkAnalogAxis${stick.xAxis}InnerDeadzone`}
									className="form-control-sm"
									groupClassName="col-sm-3 mb-3"
									value={values[`gplinkAnalogAxis${stick.xAxis}InnerDeadzone`]}
									error={errors[`gplinkAnalogAxis${stick.xAxis}InnerDeadzone`]}
									isInvalid={Boolean(
										errors[`gplinkAnalogAxis${stick.xAxis}InnerDeadzone`],
									)}
									onChange={handleChange}
									min={0}
									max={100}
								/>
								<FormControl
									type="number"
									label={t(
										'AddonsConfig:gplink-analog-axis-inner-deadzone-label',
										{ axis: stick.key === 'stick1' ? 'LY' : 'RY' },
									)}
									name={`gplinkAnalogAxis${stick.yAxis}InnerDeadzone`}
									className="form-control-sm"
									groupClassName="col-sm-3 mb-3"
									value={values[`gplinkAnalogAxis${stick.yAxis}InnerDeadzone`]}
									error={errors[`gplinkAnalogAxis${stick.yAxis}InnerDeadzone`]}
									isInvalid={Boolean(
										errors[`gplinkAnalogAxis${stick.yAxis}InnerDeadzone`],
									)}
									onChange={handleChange}
									min={0}
									max={100}
								/>
							</Row>
							<Row className="mb-3">
								<FormControl
									type="number"
									label={t(
										'AddonsConfig:gplink-analog-axis-outer-deadzone-label',
										{ axis: stick.key === 'stick1' ? 'LX' : 'RX' },
									)}
									name={`gplinkAnalogAxis${stick.xAxis}OuterDeadzone`}
									className="form-control-sm"
									groupClassName="col-sm-3 mb-3"
									value={values[`gplinkAnalogAxis${stick.xAxis}OuterDeadzone`]}
									error={errors[`gplinkAnalogAxis${stick.xAxis}OuterDeadzone`]}
									isInvalid={Boolean(
										errors[`gplinkAnalogAxis${stick.xAxis}OuterDeadzone`],
									)}
									onChange={handleChange}
									min={0}
									max={100}
								/>
								<FormControl
									type="number"
									label={t(
										'AddonsConfig:gplink-analog-axis-outer-deadzone-label',
										{ axis: stick.key === 'stick1' ? 'LY' : 'RY' },
									)}
									name={`gplinkAnalogAxis${stick.yAxis}OuterDeadzone`}
									className="form-control-sm"
									groupClassName="col-sm-3 mb-3"
									value={values[`gplinkAnalogAxis${stick.yAxis}OuterDeadzone`]}
									error={errors[`gplinkAnalogAxis${stick.yAxis}OuterDeadzone`]}
									isInvalid={Boolean(
										errors[`gplinkAnalogAxis${stick.yAxis}OuterDeadzone`],
									)}
									onChange={handleChange}
									min={0}
									max={100}
								/>
								<FormCheck
									label={t(stick.deadzoneEnabledLabel)}
									type="switch"
									id={`GPLinkAnalog${stick.key}Deadzone`}
									className="col-sm-3 ms-3"
									isInvalid={false}
									checked={Boolean(values[stick.deadzoneEnabled])}
									onChange={(e) => {
										handleCheckbox(stick.deadzoneEnabled);
										handleChange(e);
									}}
								/>
								<FormControl
									type="number"
									label={t(stick.deadzoneLabel)}
									name={stick.deadzone}
									className="form-control-sm"
									groupClassName="col-sm-3 mb-3"
									value={values[stick.deadzone]}
									error={errors[stick.deadzone]}
									isInvalid={Boolean(errors[stick.deadzone])}
									onChange={handleChange}
									min={0}
									max={100}
								/>
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
										{t('AddonsConfig:gplink-analog-live-text', {
											x: liveValues[stick.valueX],
											y: liveValues[stick.valueY],
										})}
									</span>
								</div>
							</Row>
							<Row className="mb-3">
								<FormCheck
									label={t('AddonsConfig:analog-smoothing')}
									type="switch"
									id={`GPLinkAnalog${stick.key}Smoothing`}
									className="col-sm-3 ms-3"
									isInvalid={false}
									checked={Boolean(values[stick.smoothingEnabled])}
									onChange={(e) => {
										handleCheckbox(stick.smoothingEnabled);
										handleChange(e);
									}}
								/>
								<FormControl
									hidden={!values[stick.smoothingEnabled]}
									type="number"
									label={t('AddonsConfig:smoothing-factor')}
									name={stick.smoothingFactor}
									className="form-control-sm"
									groupClassName="col-sm-3 mb-3"
									value={values[stick.smoothingFactor]}
									error={errors[stick.smoothingFactor]}
									isInvalid={Boolean(errors[stick.smoothingFactor])}
									onChange={handleChange}
									min={0}
									max={100}
								/>
							</Row>
							<Row className="mb-3">
								<FormCheck
									label={t('AddonsConfig:analog-force-circularity')}
									type="switch"
									id={`GPLinkAnalog${stick.key}Circularity`}
									className="col-sm-3 ms-3"
									isInvalid={false}
									checked={Boolean(values[stick.forcedCircularity])}
									onChange={(e) => {
										handleCheckbox(stick.forcedCircularity);
										handleChange(e);
									}}
								/>
							</Row>
						</Tab>
					))}
					<Tab eventKey="triggers" title={t('AddonsConfig:gplink-analog-triggers')}>
						<Row className="mb-3">
							<div className="col-sm-12">
								<div className="alert alert-info" role="alert">
									{t('AddonsConfig:gplink-analog-triggers-sub-header-text')}
								</div>
							</div>
						</Row>
						{TRIGGERS.map((trigger) => (
							<Row className="mb-3" key={trigger.key}>
								<FormControl
									type="number"
									label={t(trigger.pinLabel)}
									name={trigger.pin}
									className="form-control-sm"
									groupClassName="col-sm-2 mb-3"
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
								<div className="col-sm-6">
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
									<span className="text-muted">
										{t('AddonsConfig:gplink-analog-trigger-live-text', {
											v: liveValues[trigger.key],
										})}
									</span>
								</div>
							</Row>
						))}
					</Tab>
				</Tabs>
			</div>
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
			<div className="mt-2">
				<Button type="submit">{t('Common:button-save-label')}</Button>
			</div>
		</Section>
	);
};

export default GPLinkAnalog;
