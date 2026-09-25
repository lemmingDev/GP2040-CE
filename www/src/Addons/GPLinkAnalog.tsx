import { useTranslation } from 'react-i18next';
import { FormCheck, Row } from 'react-bootstrap';
import * as yup from 'yup';

import Section from '../Components/Section';
import FormControl from '../Components/FormControl';
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
};

const AXES = ['LX', 'LY', 'RX', 'RY'];

const GPLinkAnalog = ({
	values,
	errors,
	handleChange,
	handleCheckbox,
}: AddonPropTypes) => {
	const { t } = useTranslation();
	return (
		<Section title={t('AddonsConfig:gplink-analog-header-text')}>
			<div id="GPLinkAnalogOptions" hidden={!values.GPLinkAnalogEnabled}>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:gplink-analog-sub-header-text')}
				</div>
				<Row className="mb-3">
					<FormCheck
						label={t('AddonsConfig:gplink-analog-left-stick-deadzone-enabled-label')}
						type="switch"
						id="GPLinkAnalogLeftStickDeadzone"
						className="col-sm-3 ms-3"
						isInvalid={false}
						checked={Boolean(values.gplinkAnalogLeftStickDeadzoneEnabled)}
						onChange={(e) => {
							handleCheckbox('gplinkAnalogLeftStickDeadzoneEnabled');
							handleChange(e);
						}}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-left-stick-deadzone-label')}
						name="gplinkAnalogLeftStickDeadzone"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogLeftStickDeadzone}
						error={errors.gplinkAnalogLeftStickDeadzone}
						isInvalid={Boolean(errors.gplinkAnalogLeftStickDeadzone)}
						onChange={handleChange}
						min={0}
						max={100}
					/>
					<FormCheck
						label={t('AddonsConfig:gplink-analog-right-stick-deadzone-enabled-label')}
						type="switch"
						id="GPLinkAnalogRightStickDeadzone"
						className="col-sm-3 ms-3"
						isInvalid={false}
						checked={Boolean(values.gplinkAnalogRightStickDeadzoneEnabled)}
						onChange={(e) => {
							handleCheckbox('gplinkAnalogRightStickDeadzoneEnabled');
							handleChange(e);
						}}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-right-stick-deadzone-label')}
						name="gplinkAnalogRightStickDeadzone"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogRightStickDeadzone}
						error={errors.gplinkAnalogRightStickDeadzone}
						isInvalid={Boolean(errors.gplinkAnalogRightStickDeadzone)}
						onChange={handleChange}
						min={0}
						max={100}
					/>
				</Row>
				<Row className="mb-3">
					{AXES.map((axis, i) => (
						<FormControl
							key={`gplinkAnalogAxis${i}InnerDeadzone`}
							type="number"
							label={t('AddonsConfig:gplink-analog-axis-inner-deadzone-label', {
								axis,
							})}
							name={`gplinkAnalogAxis${i}InnerDeadzone`}
							className="form-control-sm"
							groupClassName="col-sm-3 mb-3"
							value={values[`gplinkAnalogAxis${i}InnerDeadzone`]}
							error={errors[`gplinkAnalogAxis${i}InnerDeadzone`]}
							isInvalid={Boolean(errors[`gplinkAnalogAxis${i}InnerDeadzone`])}
							onChange={handleChange}
							min={0}
							max={100}
						/>
					))}
				</Row>
				<Row className="mb-3">
					{AXES.map((axis, i) => (
						<FormControl
							key={`gplinkAnalogAxis${i}OuterDeadzone`}
							type="number"
							label={t('AddonsConfig:gplink-analog-axis-outer-deadzone-label', {
								axis,
							})}
							name={`gplinkAnalogAxis${i}OuterDeadzone`}
							className="form-control-sm"
							groupClassName="col-sm-3 mb-3"
							value={values[`gplinkAnalogAxis${i}OuterDeadzone`]}
							error={errors[`gplinkAnalogAxis${i}OuterDeadzone`]}
							isInvalid={Boolean(errors[`gplinkAnalogAxis${i}OuterDeadzone`])}
							onChange={handleChange}
							min={0}
							max={100}
						/>
					))}
				</Row>
			</div>
				<Row className="mb-3">
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-lx-pin-label')}
						name="gplinkAnalogLxPin"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogLxPin}
						error={errors.gplinkAnalogLxPin}
						isInvalid={Boolean(errors.gplinkAnalogLxPin)}
						onChange={handleChange}
						min={-1}
						max={63}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-ly-pin-label')}
						name="gplinkAnalogLyPin"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogLyPin}
						error={errors.gplinkAnalogLyPin}
						isInvalid={Boolean(errors.gplinkAnalogLyPin)}
						onChange={handleChange}
						min={-1}
						max={63}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-rx-pin-label')}
						name="gplinkAnalogRxPin"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogRxPin}
						error={errors.gplinkAnalogRxPin}
						isInvalid={Boolean(errors.gplinkAnalogRxPin)}
						onChange={handleChange}
						min={-1}
						max={63}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-ry-pin-label')}
						name="gplinkAnalogRyPin"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogRyPin}
						error={errors.gplinkAnalogRyPin}
						isInvalid={Boolean(errors.gplinkAnalogRyPin)}
						onChange={handleChange}
						min={-1}
						max={63}
					/>
				</Row>
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
		</Section>
	);
};

export default GPLinkAnalog;
