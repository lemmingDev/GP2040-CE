import { useContext, useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, FormCheck, Row } from 'react-bootstrap';
import * as yup from 'yup';

import { AppContext } from '../Contexts/AppContext';
import Section from '../Components/Section';
import FormSelect from '../Components/FormSelect';
import WebApi from '../Services/WebApi';
import useExpansionPinStore from '../Store/useExpansionPinStore';
import { BUTTON_ACTIONS } from '../Data/Pins';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

// RP2040 UART GPIO-mux tables. Source of truth: extras/gp-link/gplink_link.h
// (gplink_uart0_pins_valid / gplink_uart1_pins_valid). GP24/29 excluded:
// Pico onboard LED / VSYS ADC.
const GPLINK_UART_PINS = {
	0: { tx: [0, 12, 16, 28], rx: [1, 13, 17] },
	1: { tx: [4, 8, 20], rx: [5, 9, 21] },
};

const GPLINK_DEFAULT_PINS = {
	0: { tx: 0, rx: 1 },
	1: { tx: 8, rx: 9 },
};

const GPLINK_BAUD_RATES = [
	{ label: '2000000 (2 Mbaud, default)', value: 2000000 },
	{ label: '921600 (fallback)', value: 921600 },
];

// Honored by applyGpioMask in src/addons/gplink.cpp: everything that maps
// directly onto gamepad state. NOT offered: pin-scan consumers (turbo,
// macro, sustain/focus modes, DDI, reverse), combo masks, or RESERVED /
// ASSIGNED_TO_ADDON — offering those would silently do nothing.
const SELECTABLE_BUTTON_ACTIONS = [
	-10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 59, 60, 61, 62,
	63, 64, 65, 66,
];

const GPLINK_PIN_COUNT = 64;

const pinName = (i: number) => `pin${String(i).padStart(2, '0')}`;

export const gplinkScheme = {
	GPLinkEnabled: yup.number().required().label('GPLink Enabled'),
	gplinkUartInstance: yup
		.number()
		.label('GPLink UART Instance')
		.validateRangeWhenValue('GPLinkEnabled', 0, 1),
	gplinkTxPin: yup
		.number()
		.label('GPLink TX Pin')
		.validatePinWhenValue('GPLinkEnabled'),
	gplinkRxPin: yup
		.number()
		.label('GPLink RX Pin')
		.validatePinWhenValue('GPLinkEnabled'),
	gplinkBaudRate: yup
		.number()
		.label('GPLink Baud Rate')
		.validateRangeWhenValue('GPLinkEnabled', 9600, 2000000),
};

export const gplinkState = {
	GPLinkEnabled: 0,
	gplinkUartInstance: 1,
	gplinkTxPin: 8,
	gplinkRxPin: 9,
	gplinkBaudRate: 2000000,
};

const pinOptions = (
	pins: number[],
	current: number,
	usedPins: number[] | undefined,
) =>
	pins
		.filter((pin) => pin === current || !usedPins?.includes(pin))
		.map((pin) => (
			<option key={pin} value={pin}>
				{pin}
				{usedPins?.includes(pin) && pin !== current ? ' (in use)' : ''}
			</option>
		));

const GPLink = ({
	values,
	errors,
	handleChange,
	handleCheckbox,
	setFieldValue,
}: AddonPropTypes) => {
	const { t } = useTranslation();
	const { usedPins } = useContext(AppContext);

	const instance = values.gplinkUartInstance === 0 ? 0 : 1;
	const uartPins = GPLINK_UART_PINS[instance];

	const [status, setStatus] = useState(null);
	const [discovery, setDiscovery] = useState(null);
	const [testing, setTesting] = useState(false);
	const { pins, fetchPins, setPinAction, savePins } = useExpansionPinStore();

	useEffect(() => {
		fetchPins();
	}, []);

	useEffect(() => {
		if (!values.GPLinkEnabled) {
			setStatus(null);
			return;
		}
		let cancelled = false;
		const poll = async () => {
			const data = await WebApi.getGPLinkStatus();
			if (!cancelled && data) setStatus(data);
		};
		poll();
		const timer = setInterval(poll, 1000);
		return () => {
			cancelled = true;
			clearInterval(timer);
		};
	}, [values.GPLinkEnabled]);

	const handleInstanceChange = (e) => {
		const next = parseInt(e.target.value, 10) === 0 ? 0 : 1;
		setFieldValue('gplinkUartInstance', next);
		setFieldValue('gplinkTxPin', GPLINK_DEFAULT_PINS[next].tx);
		setFieldValue('gplinkRxPin', GPLINK_DEFAULT_PINS[next].rx);
		handleChange(e);
	};

	const handleTest = async () => {
		setTesting(true);
		setDiscovery(null);
		const data = await WebApi.testGPLink();
		if (data) setDiscovery(data);
		setTesting(false);
	};

	// Rows come from discovery when available (companion-reported pins),
	// otherwise all 64 slots are shown for offline configuration.
	const rowPins =
		discovery?.found && discovery?.capsPins?.length
			? discovery.capsPins
			: Array.from({ length: GPLINK_PIN_COUNT }, (_, i) => i);

	return (
		<Section
			title={
				<a
					href="https://gp2040-ce.info/add-ons/gplink"
					target="_blank"
					className="text-reset text-decoration-none"
				>
					{t('AddonsConfig:gplink-header-text')}
				</a>
			}
		>
			<div id="GPLinkOptions" hidden={!values.GPLinkEnabled}>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:gplink-sub-header-text')}
				</div>
				<div className="alert alert-warning" role="alert">
					{t('AddonsConfig:gplink-config-mode-note-text')}
				</div>
				{instance === 0 && (
					<div className="alert alert-warning" role="alert">
						{t('AddonsConfig:gplink-uart0-warning-text')}
					</div>
				)}
				{status && (
					<div
						className={`alert ${status.linkAlive ? 'alert-success' : 'alert-secondary'}`}
						role="alert"
					>
						{t('AddonsConfig:gplink-status-text', {
							link: status.linkAlive
								? t('AddonsConfig:gplink-status-up')
								: t('AddonsConfig:gplink-status-down'),
							tx: status.txSeq,
							rx: status.ignoredFrames,
							handled: status.handledFrames ?? 0,
							gaps: status.seqGaps,
						})}
						{!status.started &&
							` ${t('AddonsConfig:gplink-status-not-started-text')}`}
					</div>
				)}
				<Row className="mb-3">
					<div className="col-sm-12">
						<Button
							size="sm"
							disabled={testing}
							onClick={handleTest}
						>
							{testing
								? t('AddonsConfig:gplink-test-running-label')
								: t('AddonsConfig:gplink-test-label')}
						</Button>
					</div>
					{discovery && (
						<div
							className={`col-sm-12 mt-2 alert ${discovery.found ? 'alert-success' : 'alert-warning'}`}
							role="alert"
						>
							{t('AddonsConfig:gplink-test-result-text', {
								cont:
									discovery.continuity === 1
										? t('AddonsConfig:gplink-status-loop-pass')
										: discovery.continuity === 2
											? t('AddonsConfig:gplink-status-loop-driven')
											: t('AddonsConfig:gplink-status-loop-open'),
								name: discovery.capsName || '?',
								count: discovery.capsCount ?? 0,
								rxb: discovery.rxBytes ?? 0,
								rxf: discovery.rxFrames ?? 0,
							})}
							{!discovery.found &&
								` ${t('AddonsConfig:gplink-test-not-found-text')}`}
						</div>
					)}
				</Row>
				<Row className="mb-3">
					<FormSelect
						label={t('AddonsConfig:gplink-instance-label')}
						name="gplinkUartInstance"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={instance}
						error={errors.gplinkUartInstance}
						isInvalid={Boolean(errors.gplinkUartInstance)}
						onChange={handleInstanceChange}
					>
						<option value={0}>UART0 (GPIO 0/1 expansion header)</option>
						<option value={1}>UART1 (default)</option>
					</FormSelect>
					<FormSelect
						label={t('AddonsConfig:gplink-tx-pin-label')}
						name="gplinkTxPin"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkTxPin}
						error={errors.gplinkTxPin}
						isInvalid={Boolean(errors.gplinkTxPin)}
						onChange={handleChange}
					>
						{pinOptions(uartPins.tx, values.gplinkTxPin, usedPins)}
					</FormSelect>
					<FormSelect
						label={t('AddonsConfig:gplink-rx-pin-label')}
						name="gplinkRxPin"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkRxPin}
						error={errors.gplinkRxPin}
						isInvalid={Boolean(errors.gplinkRxPin)}
						onChange={handleChange}
					>
						{pinOptions(uartPins.rx, values.gplinkRxPin, usedPins)}
					</FormSelect>
					<FormSelect
						label={t('AddonsConfig:gplink-baud-label')}
						name="gplinkBaudRate"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkBaudRate}
						error={errors.gplinkBaudRate}
						isInvalid={Boolean(errors.gplinkBaudRate)}
						onChange={handleChange}
					>
						{GPLINK_BAUD_RATES.map((o) => (
							<option key={`baud-option-${o.value}`} value={o.value}>
								{o.label}
							</option>
						))}
					</FormSelect>
				</Row>
				<Row className="mb-3">
					<div className="col-sm-12">
						<h6>{t('AddonsConfig:gplink-pins-header-text')}</h6>
						<p className="text-muted">
							{t('AddonsConfig:gplink-pins-sub-header-text')}
						</p>
					</div>
					{rowPins.map((pin) => {
						const name = pinName(pin);
						const current =
							pins.gplink?.[0]?.[name]?.option ?? -10;
						return (
							<FormSelect
								key={`gplink-${name}`}
								label={`Pin ${pin}`}
								name={`gplink-${name}`}
								className="form-select-sm"
								groupClassName="col-sm-3 mb-2"
								value={current}
								onChange={(e) =>
									setPinAction(
										'gplink',
										0,
										name,
										parseInt(e.target.value, 10),
									)
								}
							>
								{Object.entries(BUTTON_ACTIONS)
									.filter(([, value]) =>
										SELECTABLE_BUTTON_ACTIONS.includes(value),
									)
									.map(([key, value]) => (
										<option key={`gplink-${name}-${value}`} value={value}>
											{key}
										</option>
									))}
							</FormSelect>
						);
					})}
					<div className="col-sm-12 mt-2">
						<Button
							size="sm"
							onClick={() => {
								savePins();
							}}
						>
							{t('AddonsConfig:gplink-pins-save-label')}
						</Button>
					</div>
				</Row>
			</div>
			<FormCheck
				label={t('Common:switch-enabled')}
				type="switch"
				id="GPLinkButton"
				reverse
				isInvalid={false}
				checked={Boolean(values.GPLinkEnabled)}
				onChange={(e) => {
					handleCheckbox('GPLinkEnabled');
					handleChange(e);
				}}
			/>
		</Section>
	);
};

export default GPLink;
