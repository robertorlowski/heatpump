import {Attributes, Component, ComponentChildren, createRef, Ref} from 'preact';
import './style.css';
import '../../api/api'
import { HpRequests } from '../../api/api';
import { TSaveCO, TSettings, TSlot } from '../../api/type';

function format(num?: Number) :String {
	if (num == undefined) {
		return "";
	}
	return num.toString().padStart(2, '0');
}

interface IState {
    data?: TSettings;
	value?: TSaveCO;
	error?: boolean;
}

export default class Settings extends Component<{}, IState> {
	inputRef = createRef();

	render({}, {}) {
		if (!this.state.data  || !this.state.value) {
			return 'Loading...';
		}
		return (
			
			<div class="settings" >
				<h2>Główne ustawienia</h2>
				<section>
					<div class="resource">
						<h3>Ustaw</h3>
						<hr/>	
						<div style="min-width: 240px;">
							<span class ="label">Tryb pracy:</span>
							<select class="dict-select" 
								onChange={(e) => { this.state.value.work_mode = e.currentTarget.value }}
								value={this.state.value.work_mode}
							>
								<option value="M">ręczny</option>
								<option value="A">automatyczny</option>
								<option value="PV">automatyczny z PV</option>
								<option value="CWU">CWU</option>
								<option value="OFF">OFF</option>
							</select>
						</div>
						<div style="min-width: 240px;">
							<span class ="label">Temperatura:</span>
							<input
								class="temperature"
								type="number"
								name="min"
								value={this.state.value.temperature_co_min}
								onChange={(e) => { this.state.value.temperature_co_min = e.currentTarget.valueAsNumber }}
							/>
							<input
								class="temperature"
								type="number"
								name="max"
								value={this.state.value.temperature_co_max}
								onChange={(e) => { this.state.value.temperature_co_max = e.currentTarget.valueAsNumber }}
							/>
						</div>

						<div style="min-width: 240px;">
							<span class ="label">Wymuś pracę:</span>
							<input
								title="Wymuś pracę:"
								type="checkbox"
								checked={this.state.value.force ? true : false}
								onChange={(e) => { this.state.value.force = e.currentTarget.checked }}
							/> 
						</div>	
						<div style="min-width: 240px;">
							<span class ="label">Pompa zimnej wody:</span>
							<input
								title="Pompa zimnej wody"
								type="checkbox"
								name="coldPomp"
								checked={this.state.value.cold_pomp ? true : false}
								onChange={(e) => { this.state.value.cold_pomp = e.currentTarget.checked }}
							/>
						</div>	
						<div style="min-width: 240px;">						
							<span class ="label">Pompa ciepłej wody:</span>
							<input
								title="Pompa ciepłej wody"
								type="checkbox"
								name="hotPomp"
								checked={this.state.value.hot_pomp ? true : false}
								onChange={(e) => { this.state.value.hot_pomp = e.currentTarget.checked}}
							/> 
						</div>	
						<div style="min-width: 240px;">
							<span class ="label">Grzałka krateru:</span>
							<input
								title="Grzałka krateru"
								name="sumpHeater"
								type="checkbox"
								checked={this.state.value.sump_heater ? true : false}
								onChange={(e) => { this.state.value.sump_heater = e.currentTarget.checked }}
							/> 
						</div>
						<p>
							<button type="button" onClick={(e) => this.handleSave(this.state.value)}>Zapisz ustawienia</button>
							<span class={this.state.error ? `error show` : `error hide` }> 
								Wystąpił błąd podczas wykonywania operacji..
							</span>
						</p>	
					</div>
					<br/>
					<this.resource
						title="Automatyczny start CO"
						description = "Przedziały czasu w którym nastąpi włączenie HP."
						data= {this.state.data.settings}
					/>
					<br/>
					<this.resource
						title="Wyłączenie wykorzystania mocy z PV"
						description = "Przedziały czasu w którym nastąpi wyłączenie weryfikacji wytwarzanej mocy na panelach fotowoltaicznych."
						data = {[this.state.data.night_hour]}
					/>
				</section>
			</div>
		)
	}

	componentWillMount() {
		this.setState({ error: false });
		HpRequests.getSettings().then(resp => {
			this.setState({ data: resp });
		   }
		).catch(err =>{
			console.log(err);
		   }
		);

		HpRequests.getCoData().then(resp => {
			this.setState(
				{
					value: { 
						force: false,
						work_mode: resp.work_mode,
						cold_pomp: false,
						hot_pomp: false,
						sump_heater: false,
						temperature_co_min: resp.HP.Tcwu_min ? resp.HP.Tcwu_min : 30,
						temperature_co_max: resp.HP.Tcwu_max ? resp.HP.Tcwu_max : 40
					}
				}
			)
		}).catch(err =>{
			console.log(err);
		   }
		);
	};

	handleSave(value: TSaveCO) {
		// this.state.value.force = data.force != undefined  ? data.force : this.state.value.force;
		// this.state.value.work_mode = data.work_mode != undefined ? data.work_mode : this.state.value.work_mode;
		// this.state.value.cold_pomp = data.cold_pomp != undefined ? data.cold_pomp : this.state.value.cold_pomp;
		// this.state.value.hot_pomp = data.hot_pomp != undefined ? data.hot_pomp : this.state.value.hot_pomp;
		// this.state.value.sump_heater = data.sump_heater != undefined ? data.sump_heater : this.state.value.sump_heater;
		// this.state.value.temperature_co_max = data.temperature_co_max != undefined ? data.temperature_co_max : this.state.value.temperature_co_max;
		// this.state.value.temperature_co_min = data.temperature_co_min != undefined ? data.temperature_co_min : this.state.value.temperature_co_min;

		// console.log(value);

		HpRequests.setCoData(value)
		.then(response => {
			this.setState(
				{
					error: response.status == 200 ? false : true
				}
			)
		});
	};

	onInput(e) {
		if (e.currentTarget.value.length <= 3) {
			setValue(e.currentTarget.value);
		}
	}

	resource(props: {title, description :String, data: TSlot[] }) {
		let slotList = [];

		for (let index = 0; index < props.data.length; index++) {
			const element = props.data[index];
			slotList.push(<li key={index}> 
				{format(element.slot_start_hour)}:{format(element.slot_start_minute)} <span/>
				<strong>-</strong><span/> {format(element.slot_stop_hour)}:{format(element.slot_stop_minute)}   
			</li>);
		}
	
		return (
			<div class="resource">
				<h3>{props.title}</h3>
				{props.description}
				<hr/>
				<ul>{slotList}</ul>
			</div>			
		);
	}	
	
}


