import { useContext, useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { FormCheck, Row } from 'react-bootstrap';
import * as yup from 'yup';

import { AppContext } from '../Contexts/AppContext';
import Section from '../Components/Section';
import FormSelect from '../Components/FormSelect';
import WebApi from '../Services/WebApi';
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
							gaps: status.seqGaps,
						})}
						{!status.started &&
							` ${t('AddonsConfig:gplink-status-not-started-text')}`}
						{status.started && (
							<>
								<br />
								{t('AddonsConfig:gplink-status-mux-text', {
									txm: status.txFuncOk ? 'UART' : 'STOLEN',
									rxm: status.rxFuncOk ? 'UART' : 'STOLEN',
									fr: `0x${status.uartFr.toString(16)}`,
								})}
								<br />
								{t('AddonsConfig:gplink-status-loop-text', {
									loop:
										status.loopTest === 1
											? t('AddonsConfig:gplink-status-loop-pass')
											: status.loopTest === 2
												? t('AddonsConfig:gplink-status-loop-fail')
												: t('AddonsConfig:gplink-status-loop-na'),
								})}
								<br />
								{t('AddonsConfig:gplink-status-dispatch-text', {
									proc: status.processCalls,
									rxb: status.rxBytes,
									up: status.uptimeS,
								})}
							</>
						)}
					</div>
				)}
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
