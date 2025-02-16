import {Component} from 'preact';
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
						force: resp.HP ? resp.HP.F : false,
						work_mode: resp.work_mode
					}
				}
			)
		}).catch(err =>{
			console.log(err);
		   }
		);
	};

	onSaveData(data: TSaveCO) {
		this.state.value.force = data.force != undefined  ? data.force : this.state.value.force;
		this.state.value.work_mode = data.work_mode != undefined ? data.work_mode : this.state.value.work_mode;

		HpRequests.setCoData(this.state.value)
		.then(response => {
			this.setState(
				{
					error: response.status == 200 ? false : true
				}
			)
		});
	};

	render({}, {}) {
		if (!this.state.data  || !this.state.value) {
			return 'Loading...';
		}
		return (
			
			<div class="settings">
				<h2>Główne ustawienia</h2>
				<section>
					<div class="resource">
						<h3>Ustaw</h3>
						<hr/>	
						<div style="min-width: 240px;">
						<div>
							<span class ="label">Tryb pracy:</span>
							<select class="dict-select" 
								onChange={(e) => this.onSaveData( {work_mode: e.currentTarget.value })}
								value={this.state.value.work_mode}
							>
								<option value="M">ręczny</option>
								<option value="A">automatyczny</option>
								<option value="PV">automatyczny z PV</option>
								<option value="OFF">OFF</option>
							</select>
							{/* 							
							<input
								title="Force:"
								type="checkbox"
								checked={this.state.value.force ? true : false}
								onChange={(e) => this.onSaveData({ force: e.currentTarget.checked })}
							/> */}
						</div>
							
						</div>
						<p>
							<span class={this.state.error ? `error show` : `error hide` }> 
								Wystąpił błąd podczas wykonywania operacji..
							</span>
						</p>	
					</div>
					<br/>
					<this.resource
						title="Automatyczny start HP"
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


